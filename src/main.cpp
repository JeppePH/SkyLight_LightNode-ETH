// Designed by Jeppe Holm @ Desorb, (c) 2024, info@desorb.dk
// SPDX-FileCopyrightText: (c) 2021-2023 Shawn Silverman <shawn@pobox.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Inspired by Artnet library for Teensy by natcl: https://github.com/natcl/Artnet
// Parallel output thanks to https://github.com/PaulStoffregen/OctoWS2811/blob/master/examples/Teensy4_PinList/Teensy4_PinList.ino

// C++ includes
#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>
#include <IntervalTimer.h>

#include <OctoWS2811.h>
#include <QNEthernet.h>

#include "artnet.h"
#include "interface.h"
#include "config.h"

using namespace qindesign::network;

#if defined(__IMXRT1062__)
extern "C" uint32_t set_arm_clock(uint32_t frequency);
#endif

// --------------------------------------------------------------------------
//  Configuration
// --------------------------------------------------------------------------
#define NUM_STRIPS          5
#define CHANNELS_PER_LED    3
#define PIN_LED_STATUS      35
#define PIN_LED_DMX         34
#define PIN_LED_POLL        33
#define UNIVERSES_BY_OUT    2

const int num_leds_pr_out = 512 * UNIVERSES_BY_OUT / CHANNELS_PER_LED;
const int startUniverse = 0;
const int maxUniverses = NUM_STRIPS * UNIVERSES_BY_OUT;

DMAMEM int displayMemory[num_leds_pr_out * NUM_STRIPS * CHANNELS_PER_LED / 4];
int drawingMemory[num_leds_pr_out * NUM_STRIPS * CHANNELS_PER_LED / 4];
byte PIN_LED_DATA[] = {23, 22, 21, 20, 19};
const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(num_leds_pr_out, displayMemory, drawingMemory, config, NUM_STRIPS, PIN_LED_DATA);

unsigned long lastUpdate = 0;

IntervalTimer dmxTimer;
IntervalTimer pollTimer;

// ArtNet setup
Artnet artnet;
bool universesReceived[maxUniverses];
bool sendFrame = 1;
int previousDataLength = 0;

// The DHCP timeout, in milliseconds. Set to zero to not wait and
// instead rely on the listener to inform us of an address assignment.
constexpr uint32_t kDHCPTimeout = 15'000; // 15 seconds

// The link timeout, in milliseconds. Set to zero to not wait and
// instead rely on the listener to inform us of a link.
constexpr uint32_t kLinkTimeout = 5'000; // 5 seconds
constexpr uint16_t kServerPort = 5000;   // 53993;

// --------------------------------------------------------------------------
//  Program State
// --------------------------------------------------------------------------
// UDP setup
EthernetUDP udp; // Declare the UDP object globally

// --------------------------------------------------------------------------
//  Declarations
// --------------------------------------------------------------------------
bool initEthernet();
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t *data, IPAddress remoteIP);
void updateLEDs();
void initializeLEDs();

// --------------------------------------------------------------------------
//  Interrupts
// --------------------------------------------------------------------------
void turnOffLEDDmx() {
    digitalWrite(PIN_LED_DMX, LOW);
}

void turnOffLEDPoll() {
    digitalWrite(PIN_LED_POLL, LOW);
}


// --------------------------------------------------------------------------
//  Main Setup
// --------------------------------------------------------------------------
void setup()
{
    set_arm_clock(600000000); // Set Teensy clock to 600 MHz
    delay(1000);
    Serial.begin(115200);
    while (!Serial && millis() < 4000)
    {
        // Wait for Serial
    }
    printf("Starting...\r\n");

    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_LED_DMX, OUTPUT);
    pinMode(PIN_LED_POLL, OUTPUT);

    // Initialize SD card
    if (!SD.begin(chipSelect))
    {
        Serial.println("Failed to initialize SD card");
    }
    else
    {
        Serial.println("SD card initialized");
        loadSettingsFromSD(); // Load settings if available
    }

    // Initialize OctoWS2811 with the loaded settings
    initializeLEDs();

    // Teensy's internal MAC retrieved (or use the one defined)
    
    // printf("MAC = %02x:%02x:%02x:%02x:%02x:%02x\r\n",
    //        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Initialize Ethernet with the loaded settings
    if (initEthernet())
    {
        udp.begin(kServerPort);

        byte ipBytes[4];
        for (int i = 0; i < 4; i++)
        {
            ipBytes[i] = staticIP[i];
        }

        byte snBytes[4];
        for (int i = 0; i < 4; i++)
        {
            snBytes[i] = subnetMask[i];
        }

        // Start ArtNet
        artnet.begin(mac, ipBytes);
        artnet.setBroadcastAuto(ipBytes, snBytes);

        // Set the ArtDmx callback
        artnet.setArtDmxCallback(onDmxFrame);

        // Set up web server
        setupWebServer();
    }
    digitalWrite(PIN_LED_STATUS, HIGH);
}

bool initEthernet()
{
    // Static IP
    printf("Starting Ethernet with static IP...\r\n");
    // Ethernet.macAddress(mac);
    
    if (!Ethernet.begin(mac, staticIP, subnetMask, gateway))
    {
        printf("Failed to start Ethernet\r\n");
        return false;
    }

    // Optionally wait for the link
    if (kLinkTimeout > 0)
    {
        printf("Waiting for link...\r\n");
        if (!Ethernet.waitForLink(kLinkTimeout))
        {
            printf("No link yet\r\n");
        }
    }
    return true;
}

// --------------------------------------------------------------------------
//  Main Program
// --------------------------------------------------------------------------
void loop()
{
    unsigned long currentTime = millis();
    if (currentTime - lastUpdate >= (1000 / 60)) //(1000 / updateSpeed)
    {
        updateLEDs();
        lastUpdate = currentTime;
    }

    // Handle ArtNet data
    uint16_t packetType = artnet.read();
    if (packetType == ART_DMX)
    {
        digitalWrite(PIN_LED_DMX, HIGH);
        dmxTimer.begin(turnOffLEDDmx, 5000); // 5ms
    }
    else if (packetType == ART_POLL)
    {
        digitalWrite(PIN_LED_POLL, HIGH);
        pollTimer.begin(turnOffLEDPoll, 100000); // 200ms
    }

    handleWebServer(); // Call this to handle web server requests
    
}


// --------------------------------------------------------------------------
//  Functions
// --------------------------------------------------------------------------
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t *data, IPAddress remoteIP) {
    int stripIndex = (universe - startUniverse) / UNIVERSES_BY_OUT;
    if (stripIndex < 0 || stripIndex >= NUM_STRIPS) {
        return;
    }

    int ledOffset = (universe % UNIVERSES_BY_OUT) * (512 / CHANNELS_PER_LED);

    Serial.print("Universe: ");
    Serial.print(universe);
    Serial.print(", StripIndex: ");
    Serial.print(stripIndex);
    Serial.print(", LedOffset: ");
    Serial.println(ledOffset);

    for (int i = 0; i < min(length / CHANNELS_PER_LED, num_leds_pr_out); i++) {
        int actualLedIndex = ledOffset + i;
        leds.setPixel((stripIndex * num_leds_pr_out) + actualLedIndex,
                      data[i * CHANNELS_PER_LED],
                      data[i * CHANNELS_PER_LED + 1],
                      data[i * CHANNELS_PER_LED + 2]);
    }
}

void updateLEDs() {
    leds.show();
}


void initializeLEDs()
{
    // Map ledType and colorOrder to OctoWS2811 configurations
    int ledConfig = WS2811_800kHz; // Default
    if (ledType == "WS2811")
    {
        ledConfig |= WS2811_GRB; // Default color order
    }
    else if (ledType == "WS2812")
    {
        ledConfig |= WS2811_GRB;
    }
    else if (ledType == "WS2813")
    {
        ledConfig |= WS2811_GRB;
    }

    // Set color order
    if (colorOrder == "GRB")
    {
        ledConfig |= WS2811_GRB;
    }
    else if (colorOrder == "RGB")
    {
        ledConfig |= WS2811_RGB;
    }
    else if (colorOrder == "BRG")
    {
        ledConfig |= WS2811_BRG;
    }

    // Initialize OctoWS2811
    leds = OctoWS2811(num_leds_pr_out, displayMemory, drawingMemory, ledConfig, NUM_STRIPS, PIN_LED_DATA);
    leds.begin();
    leds.show();
}
