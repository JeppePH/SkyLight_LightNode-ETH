#include "hardware2.h"
#include "artnet.h"
#include "interface.h"
#include "config.h"
#include <OctoWS2811.h>
#include <QNEthernet.h>
using namespace qindesign::network;

#define CHANNELS_PER_LED 3
#define CHANNELS_PER_UNI 510
#define LEDS_PER_UNI (CHANNELS_PER_UNI / CHANNELS_PER_LED)

enum RunMode : uint8_t
{
    MODE_ART_TIMED = 0,
    MODE_ART_SYNC = 1,
    MODE_TEST_WHITE = 2,
    MODE_TEST_RAINBOW = 3
};

// Return true if Ethernet link is up (works with QNEthernet)
static bool linkIsUp()
{
#ifdef LinkON // present in Arduino Ethernet-style APIs
    return (Ethernet.linkStatus() == LinkON);
#else
    // Fallback: treat nonzero as "up" if LinkON isn't defined
    return Ethernet.linkStatus();
#endif
}

// How many universes per output strip (edit if you change per-strip length)
static uint8_t gUniversesPerOut = 2; // 2 * 170 = 340 px/strip by default

// Derivatives (computed after config/DIP)
static uint16_t gLedsPerStrip;

static RunMode gMode = MODE_ART_SYNC;

// Allocate for worst case: up to 4 universes/strip = 680 px/strip
constexpr int kMaxUniversesPerOut = 4;
constexpr int kMaxLedsPerStrip = LEDS_PER_UNI * kMaxUniversesPerOut; // 170 * 4 = 680

DMAMEM int displayMemory[kMaxLedsPerStrip * 6];
int drawingMemory[kMaxLedsPerStrip * 6];

OctoWS2811 *leds = nullptr;

Artnet artnet;
volatile bool gGotArtSync = false;
volatile bool gFrameDirty = false;
uint16_t gStartUniverse = 0;
uint16_t gFpsFallback = 60;
static uint32_t lastUpdate = 0;

static void onArtSync(IPAddress) { gGotArtSync = true; }
static void setStatus(bool on) { ledWrite(PIN_LED_STATUS, on); }

// Drive status LED from real Ethernet link state (QNEthernet)
static void setupLinkLED()
{
    // Immediate state
    setStatus(Ethernet.linkState());
    // Live updates on link changes
    Ethernet.onLinkState([](bool state)
                         {
    setStatus(state);  // ON when link is UP
    Serial.printf("Link %s\n", state ? "UP" : "DOWN"); });
}

static void initLEDs()
{
    int cfg = WS2811_800kHz | (colorOrder == "RGB" ? WS2811_RGB : colorOrder == "BRG" ? WS2811_BRG
                                                                                      : WS2811_GRB);
    leds = new OctoWS2811(gLedsPerStrip, displayMemory, drawingMemory, cfg, kNumOutputs, (byte *)kDataPins);
    leds->begin();
    leds->show();
}

static void onDmxFrame(uint16_t uni, uint16_t len, uint8_t, uint8_t *data, IPAddress)
{
    if (!leds)
        return;
    int rel = uni; // startUniverse = 0; adjust if you add that later
    int out = rel / gUniversesPerOut;
    if (out < 0 || out >= kNumOutputs)
        return;
    int uIn = rel % gUniversesPerOut;
    int base = out * gLedsPerStrip + uIn * 170; // 170 px / universe
    int n = min((int)(len / 3), 170);
    for (int i = 0; i < n; ++i)
    {
        int di = i * 3;
        leds->setPixel(base + i, data[di], data[di + 1], data[di + 2]);
    }
    gFrameDirty = true;
    ledWrite(PIN_LED_DMX, true);
}

void setup()
{
    Serial.begin(115200);
    ledWrite(PIN_LED_STATUS, false);
    ledWrite(PIN_LED_DMX, false);
    ledWrite(PIN_LED_POLL, false);

    // if (SD.begin(BUILTIN_SDCARD)) loadSettingsFromSD();

    dipInit();

    auto applyDipBootConfig = []()
    {
        const uint8_t dip = readDip8(); // normalized so "ON"=1 if DIP_ACTIVE_LOW=true

        // --- Decode fields ---
        const uint8_t ipOff = (dip & 0x0F);         // DIP 1–4
        const uint8_t modeV = ((dip >> 4) & 0x03);  // DIP 5–6
        const uint8_t upoSel = ((dip >> 6) & 0x03); // DIP 7–8

        // Apply: Mode
        gMode = static_cast<RunMode>(modeV);

        // Apply: Universes per output (1..4)
        gUniversesPerOut = 1 + upoSel;                   // 1..4
        gLedsPerStrip = LEDS_PER_UNI * gUniversesPerOut; // 170 px per universe

        // Apply: IP last-octet offset
        IPAddress ip = staticIP;
        ip[3] = static_cast<uint8_t>(ip[3] + ipOff);
        staticIP = ip;

        // --- Debug printout ---
        char bits[9];
        for (int i = 7; i >= 0; --i)
            bits[7 - i] = ((dip >> i) & 0x01) ? '1' : '0';
        bits[8] = '\0';

        const char *modeStr =
            (gMode == MODE_ART_TIMED) ? "ART_TIMED" : (gMode == MODE_ART_SYNC) ? "ART_SYNC"
                                                  : (gMode == MODE_TEST_WHITE) ? "TEST_WHITE"
                                                                               : "TEST_RAINBOW";

        Serial.printf("DIP=0x%02X (bits 8..1: %s)\r\n", dip, bits);
        Serial.printf("  IP offset: +%u  ->  %d.%d.%d.%d\r\n",
                      ipOff, staticIP[0], staticIP[1], staticIP[2], staticIP[3]);
        Serial.printf("  Mode: %s (%u)\r\n", modeStr, static_cast<unsigned>(gMode));
        Serial.printf("  Universes/Output: %u  (LEDs/strip: %u)\r\n",
                      static_cast<unsigned>(gUniversesPerOut),
                      static_cast<unsigned>(gLedsPerStrip));
    };
    applyDipBootConfig();

    // NO: gLedsPerStrip = LEDS_PER_UNI * gUniversesPerOut;  // already done above

    initLEDs();

    byte ipBytes[4], snBytes[4];
    for (int i = 0; i < 4; i++)
    {
        ipBytes[i] = staticIP[i];
        snBytes[i] = subnetMask[i];
    }
    artnet.begin(mac, ipBytes);
    artnet.setBroadcastAuto(staticIP, subnetMask);
    artnet.setArtDmxCallback(onDmxFrame);
    artnet.setArtSyncCallback(onArtSync);

    setupLinkLED(); // reflect real cable link on the status LED

    // Do NOT force status LED on here; it's handled by link state now.
}

void loop()
{
    // Drain all pending Art-Net packets this iteration
    for (;;)
    {
        uint16_t pktType = artnet.read();
        if (pktType == 0)
            break; // no more packets

        if (pktType == ART_POLL)
        {
            ledWrite(PIN_LED_POLL, true);
        }
        // ART_DMX and ART_SYNC are handled via callbacks:
        //   onDmxFrame() sets gFrameDirty + pulses DMX LED
        //   onArtSync()  sets gGotArtSync
    }

    const uint32_t now = millis();
    const uint32_t fps = (updateSpeed > 0 ? updateSpeed : 60);
    const uint32_t period = 1000UL / fps;

    switch (gMode)
    {
    case MODE_ART_SYNC:
        // Latch on ArtSync (preferred) or timed fallback if no sync arrives
        if ((gGotArtSync && gFrameDirty) || ((now - lastUpdate) >= period && gFrameDirty))
        {
            if (leds)
                leds->show();
            gGotArtSync = false;
            gFrameDirty = false;
            lastUpdate = now;
            ledWrite(PIN_LED_DMX, false);
            ledWrite(PIN_LED_POLL, false);
        }
        break;

    case MODE_ART_TIMED:
        // Fixed-rate latching regardless of ArtSync
        if (now - lastUpdate >= period)
        {
            if (leds)
                leds->show();
            lastUpdate = now;
            ledWrite(PIN_LED_DMX, false);
            ledWrite(PIN_LED_POLL, false);
        }
        break;

    case MODE_TEST_WHITE:
    {
        static bool initialized = false;
        if (!initialized && leds)
        {
            const int total = gLedsPerStrip * kNumOutputs;
            for (int i = 0; i < total; ++i)
                leds->setPixel(i, 255, 255, 255);
            leds->show();
            initialized = true;
        }
        break;
    }

    case MODE_TEST_RAINBOW:
    {
        static uint16_t hue = 0;
        static uint32_t lastAnim = 0;
        if (now - lastAnim >= 16)
        { // ~60 Hz
            lastAnim = now;
            if (leds)
            {
                const int total = gLedsPerStrip * kNumOutputs;
                for (int i = 0; i < total; ++i)
                {
                    uint8_t v = (uint8_t)((i * 3 + hue) & 0xFF);
                    uint8_t region = v / 43, rem = v % 43;
                    uint8_t q = (uint8_t)((255 * (43 - rem)) / 43);
                    uint8_t t = (uint8_t)((255 * rem) / 43);
                    uint8_t r, g, b;
                    switch (region)
                    {
                    case 0:
                        r = 255;
                        g = t;
                        b = 0;
                        break;
                    case 1:
                        r = q;
                        g = 255;
                        b = 0;
                        break;
                    case 2:
                        r = 0;
                        g = 255;
                        b = t;
                        break;
                    case 3:
                        r = 0;
                        g = q;
                        b = 255;
                        break;
                    case 4:
                        r = t;
                        g = 0;
                        b = 255;
                        break;
                    default:
                        r = 255;
                        g = 0;
                        b = q;
                        break;
                    }
                    leds->setPixel(i, r, g, b);
                }
                leds->show();
                hue += 2;
            }
        }
        break;
    }
    }

    // If you use a web UI, call it here:
    // handleWebServer();
}
