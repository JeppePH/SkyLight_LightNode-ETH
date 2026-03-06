// main.cpp
#include <Arduino.h>
#include <SD.h>
#include <OctoWS2811.h>

#include "hardware2_3.h"
#include "config.h"
#include "artnet.h"
#include "interface.h"

// ================== PARAMETERS / CONSTANTS ==================
static const uint16_t kStartUniverse = 0; // set to 1 if your controller is 1-based
#define CHANNELS_PER_UNI 510              // 170 RGB pixels
#define CHANNELS_PER_LED 3
#define LEDS_PER_UNI (CHANNELS_PER_UNI / CHANNELS_PER_LED) // 170

// ================== GLOBAL STATE ==================
Artnet artnet;
OctoWS2811 *leds = nullptr;

static uint8_t gUniversesPerOut = 1;          // DIP 7–8 (1..4)
static uint16_t gLedsPerStrip = LEDS_PER_UNI; // = 170 * gUniversesPerOut

// DMA buffers sized for worst case: 4 segments × 170 px × 8 outs × 3 bytes
DMAMEM int displayMemory[(LEDS_PER_UNI * 4 * kNumOutputs * 3) / 4 + 32];
int drawingMemory[(LEDS_PER_UNI * 4 * kNumOutputs * 3) / 4 + 32];

int test_brightness = 50; // for test patterns

static inline uint8_t scaleTest(uint8_t v) {
    return (uint16_t(v) * test_brightness) / 255;
}

// Mode & latch
enum RunMode : uint8_t
{
    MODE_ART_TIMED = 0,
    MODE_ART_SYNC = 1,
    MODE_TEST_WHITE = 2,
    MODE_TEST_RAINBOW = 3
};
static RunMode gMode = MODE_ART_SYNC;
static volatile bool gGotArtSync = false;
static volatile bool gFrameDirty = false;
static unsigned long lastLatchMs = 0;

static bool gLinkUp = false;
static uint32_t gLastLinkCheckMs = 0;
static uint32_t pollLedTimer = 0;
static const uint16_t pollLedDurationMs = 200;

// ================== HELPERS ==================
static inline int octoIndex(uint8_t out, uint16_t pixelInStrip)
{
    // OctoWS2811 stores pixels interleaved by output
    return pixelInStrip * kNumOutputs + out;
}

static int octoConfigFromColorOrder(const String &order)
{
    int cfg = WS2811_800kHz;
    if (order == "RGB")
        cfg |= WS2811_RGB;
    else if (order == "BRG")
        cfg |= WS2811_BRG;
    else
        cfg |= WS2811_GRB; // default
    return cfg;
}

static void initOcto()
{
    if (leds)
    {
        delete leds;
        leds = nullptr;
    }
    int cfg = octoConfigFromColorOrder(colorOrder);
    leds = new OctoWS2811(gLedsPerStrip, displayMemory, drawingMemory, cfg, kNumOutputs, (byte *)kDataPins);
    leds->begin();
    leds->show();
}

static void initArtnet()
{
    artnet.begin(); // Ethernet is already up via QNEthernet
    artnet.setBroadcastAuto(Ethernet.localIP(), Ethernet.subnetMask());

    artnet.setStartUniverse(kStartUniverse); // base universe for OUT0
    // (Optional) if you later expose UPO via UI:
    // artnet.setUniversesPerOutput(gUniversesPerOut);
}

static void initNetwork()
{
    // stopWebServer();
    // Ethernet.end();
    // delay(1000);

    // Bring up Ethernet (QNEthernet supports this overload)
    IPAddress dns(0, 0, 0, 0);
    Ethernet.begin(mac, staticIP, dns, gateway, subnetMask);
    // Ethernet.setHostname("SkyLED");

    if (!Ethernet.waitForLocalIP(3000))
    {
        Serial.println("[Net] No local IP after 3s");
    }
}

static inline void blackoutAll()
{
    if (!leds)
        return;
    for (uint16_t p = 0; p < gLedsPerStrip; ++p)
    {
        for (uint8_t o = 0; o < kNumOutputs; ++o)
        {
            leds->setPixel(octoIndex(o, p), 0, 0, 0);
        }
    }
    leds->show();
    gFrameDirty = false;
    gGotArtSync = false;
}

// ================== DIP WATCHER ==================
struct DipWatcher
{
    uint8_t stable = 0, sample = 0, lastApplied = 0;
    uint32_t sampleSince = 0, lastPoll = 0;
    const uint16_t pollMs = 10, debounceMs = 40;

    void begin()
    {
        dipInit();
        sample = readDip8();
        delay(debounceMs);
        stable = readDip8();
        sample = stable;
        sampleSince = millis();
        lastPoll = millis();
        apply(stable, true);
    }
    void update()
    {
        uint32_t now = millis();
        if (now - lastPoll < pollMs)
            return;
        lastPoll = now;

        uint8_t raw = readDip8();
        if (raw != sample)
        {
            sample = raw;
            sampleSince = now;
            return;
        }
        if ((now - sampleSince) >= debounceMs && sample != stable)
        {
            stable = sample;
            apply(stable, false);
        }
    }
    void apply(uint8_t dip, bool first)
    {
        // ---- Blackout immediately on any DIP change (but skip at very first boot apply if you want) ----
        if (!first)
            blackoutAll();

        // DIP 1–4: IP last octet offset (0..15), add to config base
        // uint8_t ipOff = dip & 0x0F;
        staticIP = baseIP;
        staticIP[3] += (dip & 0x0F); // offset safely

        // DIP 5–6: Mode
        gMode = static_cast<RunMode>((dip >> 4) & 0x03);

        // DIP 7–8: Universes per output (1..4) -> LEDs per strip
        uint8_t upo = 1 + ((dip >> 6) & 0x03);
        bool lengthChanged = (upo != gUniversesPerOut);
        gUniversesPerOut = upo;
        gLedsPerStrip = (uint16_t)(LEDS_PER_UNI * gUniversesPerOut);

        // (Re)initialize subsystems as needed
        if (first || ((lastApplied & 0x0F) != (dip & 0x0F)))
        {
            // Re-apply full IP config to NIC
            
            initNetwork();
            initArtnet();
            setupWebServer(); // ensure HTTP server is listening
        }
        if (first || lengthChanged)
        {
            // ensure black before reinit (safety if first==true and you still want blackout at boot)
            // blackoutAll(); // uncomment if you ALSO want blackout at boot-time first apply
            initOcto();
        }

        lastApplied = dip;
        Serial.printf("DIP %s -> 0x%02X | IP=%d.%d.%d.%d | Mode=%u | UPO=%u | LEDs/strip=%u\n",
                      first ? "init" : "changed", dip,
                      staticIP[0], staticIP[1], staticIP[2], staticIP[3],
                      (unsigned)gMode, (unsigned)gUniversesPerOut, (unsigned)gLedsPerStrip);
    }
} gDip;

// ================== ART-NET CALLBACKS ==================
static void onArtSync(IPAddress)
{
    gGotArtSync = true;
}

static void onDmxFrame(uint16_t uni, uint16_t len, uint8_t /*seq*/, uint8_t *data, IPAddress)
{
    Serial.printf("Art-Net DMX frame: Uni=%u Len=%u\n", (unsigned)uni, (unsigned)len);
    if (!leds)
        return;
    if (uni < kStartUniverse)
        return;

    // ----- BLOCKED mapping: UPO universes per output -----
    uint32_t rel = (uint32_t)uni - (uint32_t)kStartUniverse;
    uint8_t out = (uint8_t)(rel / gUniversesPerOut);   // which output
    uint16_t seg = (uint16_t)(rel % gUniversesPerOut); // universe# within that output
    if (out >= kNumOutputs)
        return;

    // Start pixel of this universe on that output
    const uint16_t base = (uint16_t)(seg * LEDS_PER_UNI); // 0,170,340,...
    uint16_t pxInUni = (uint16_t)min((int)(len / 3), (int)LEDS_PER_UNI);

    // Clip to configured strip length (safety)
    if (base >= gLedsPerStrip)
        return;
    if (base + pxInUni > gLedsPerStrip)
        pxInUni = gLedsPerStrip - base;
    if (pxInUni == 0)
        return;

    // ----- Write pixels: CONTIGUOUS block per output -----
    const uint32_t outBase = (uint32_t)out * (uint32_t)gLedsPerStrip; // start of this output’s block
    for (uint16_t i = 0; i < pxInUni; ++i)
    {
        const int di = i * 3;                                         // R,G,B
        const int gi = (int)(outBase + base + i);                     // global linear index
        leds->setPixel(gi, data[di + 0], data[di + 1], data[di + 2]); // RGB; color order handled by Octo config
    }

    gFrameDirty = true;
    ledWrite(PIN_LED_DMX, true);
}

// ================== LATCH & TEST MODES ==================
static void latchIfDue()
{
    const unsigned long now = millis();
    const unsigned long period = 1000UL / (updateSpeed ? updateSpeed : 60);

    bool shouldLatch = false;
    if (gMode == MODE_ART_SYNC)
    {
        shouldLatch = (gGotArtSync && gFrameDirty) || ((now - lastLatchMs) >= period && gFrameDirty);
    }
    else if (gMode == MODE_ART_TIMED)
    {
        shouldLatch = ((now - lastLatchMs) >= period);
    }

    if (shouldLatch)
    {
        leds->show();
        gFrameDirty = false;
        gGotArtSync = false;
        lastLatchMs = now;
        ledWrite(PIN_LED_DMX, false);
        ledWrite(PIN_LED_POLL, false);
    }
}

static void runTestWhite()
{
    static uint32_t t0 = 0;

    uint8_t v = scaleTest(255);

    if (millis() - t0 < 500)
        return;
    t0 = millis();
    for (uint16_t p = 0; p < gLedsPerStrip; ++p)
    {
        for (uint8_t o = 0; o < kNumOutputs; ++o)
        {   
            
            leds->setPixel(octoIndex(o, p), v, v, v);
        }
    }
    leds->show();
}

static void runTestRainbow()
{
    // Tune these two to taste
    const uint16_t frameIntervalMs = 40; // time between hue steps (larger = slower)
    const uint8_t spatialStride = 2;     // how fast color changes along each strip (1 = slower than 3)

    static uint16_t h = 0;
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < frameIntervalMs)
        return; // throttle animation speed
    last = now;

    for (uint16_t p = 0; p < gLedsPerStrip; ++p)
    {
        // gentler spatial variation and slow temporal shift
        uint8_t v = (uint8_t)((p * spatialStride + h) & 255);

        // compact HSV->RGB (value=full, sat=full)
        uint8_t region = v / 43, rem = v % 43;
        uint8_t q = (uint8_t)((255 * (43 - rem)) / 43);
        uint8_t t = (uint8_t)((255 * rem) / 43);
        uint8_t r, g, b;
        switch (region)
        {
        case 0:
            r = scaleTest(r);
            g = t;
            b = 0;
            break;
        case 1:
            r = q;
            g = scaleTest(g);
            b = 0;
            break;
        case 2:
            r = 0;
            r = scaleTest(g);
            b = t;
            break;
        case 3:
            r = 0;
            g = q;
            r = scaleTest(b);
            break;
        case 4:
            r = t;
            g = 0;
            r = scaleTest(b);
            break;
        default:
            r = scaleTest(r);
            g = 0;
            b = q;
            break;
        }

        for (uint8_t o = 0; o < kNumOutputs; ++o)
        {
            leds->setPixel(octoIndex(o, p), r, g, b);
        }
    }
    leds->show();

    // Slow temporal advance (smaller step = slower)
    h = (uint16_t)((h + 1) & 0xFF);
}

// ================== SETUP / LOOP ==================
void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 1500)
    {
    }

    // Status LEDs
    ledWrite(PIN_LED_STATUS, false);
    ledWrite(PIN_LED_DMX, false);
    ledWrite(PIN_LED_POLL, false);

    // SD config
    if (SD.begin(BUILTIN_SDCARD))
    {
        loadSettingsFromSD(); // loads staticIP, subnetMask, gateway, colorOrder, updateSpeed
    }
    else
    {
        Serial.println("SD init failed; using compiled defaults.");
    }

    // Initialize subsystems using current config; DIP will immediately re-apply/override
    initDeviceMAC();
    // initNetwork();
    initOcto();
    // initArtnet();
    // setupWebServer();

    // DIP live watcher (applies IP/mode/UPO and re-inits as needed)
    gDip.begin();

    // Bind callbacks after DIP may have re-initialized Art-Net
    artnet.setArtDmxCallback(onDmxFrame);
    artnet.setArtSyncCallback(onArtSync);

    ledWrite(PIN_LED_STATUS, true);
    Serial.println("Art-Net LED node booted.");

    Serial.printf("StartUni=%u, UPO=%u, Outputs=%u, LEDs/uni=%u, LEDs/out=%u\n",
                  (unsigned)kStartUniverse, (unsigned)gUniversesPerOut,
                  (unsigned)kNumOutputs, (unsigned)LEDS_PER_UNI, (unsigned)gLedsPerStrip);
}

void loop()
{
    Ethernet.loop();   // <-- IMPORTANT: services the TCP/IP timers and RX
    gDip.update();     // DIP live update
    handleWebServer(); // <-- poll the web server every frame

    // Quick link check every ~1s
    if (millis() - gLastLinkCheckMs >= 1000)
    {
        gLastLinkCheckMs = millis();
        bool up = (Ethernet.linkStatus() == 1);
        if (up != gLinkUp)
        {
            gLinkUp = up;
            if (gLinkUp)
            {
                ledWrite(PIN_LED_STATUS, true);
                Serial.println("Link came UP. Re-init artnet + server.");
                initArtnet();     // re-apply static IP/broadcast
                setupWebServer(); // ensure HTTP server is listening
            }
            else
            {
                ledWrite(PIN_LED_STATUS, false);
                Serial.println("Link went DOWN (cable unplugged?).");
            }
        }
    }

    // Network / Art-Net
    uint16_t pkt = artnet.read();
    if (pkt == ART_POLL)
    {
        Serial.println("Art-Net Poll received.");
        ledWrite(PIN_LED_POLL, true);
        pollLedTimer = millis(); // start short blink timer
    }
    // --- Auto-off for Poll LED ---
    if (pollLedTimer && (millis() - pollLedTimer >= pollLedDurationMs))
    {
        ledWrite(PIN_LED_POLL, false);
        pollLedTimer = 0;
    }

    // Run mode
    switch (gMode)
    {
    case MODE_TEST_WHITE:
        runTestWhite();
        break;
    case MODE_TEST_RAINBOW:
        runTestRainbow();
        break;
    default:
        latchIfDue();
        break; // ART_TIMED / ART_SYNC
    }
}
