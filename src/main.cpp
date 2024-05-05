// Designed by Jeppe Holm @ Desorb, (c) 2023, info@desorb.dk
// SPDX-FileCopyrightText: (c) 2021-2023 Shawn Silverman <shawn@pobox.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Inspired by Artnet library for Teensy by natcl: https://github.com/natcl/Artnet

// C++ includes
#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "artnet.h"
#include <OctoWS2811.h>
#include <FastLED.h>
#include <QNEthernet.h>

using namespace qindesign::network;

#if defined(__IMXRT1062__)
extern "C" uint32_t set_arm_clock(uint32_t frequency);
#endif

// --------------------------------------------------------------------------
//  Configuration
// --------------------------------------------------------------------------
#define NUM_LEDS_PER_STRIP 240
#define NUM_STRIPS 5
#define CHANNELS_PER_LED 3
#define PIN_LED_STATUS 35
#define PIN_LED_DMX 34
#define PIN_LED_POLL 33

const int universes_by_out = 2;
const int startUniverse = 0;
const int maxUniverses = NUM_STRIPS * universes_by_out;

const unsigned long updateInterval = 10; // Update interval in milliseconds

DMAMEM int displayMemory[NUM_LEDS_PER_STRIP * NUM_STRIPS * CHANNELS_PER_LED / 4];
int drawingMemory[NUM_LEDS_PER_STRIP * NUM_STRIPS * CHANNELS_PER_LED / 4];
// OctoWS2811 leds(NUM_LEDS_PER_STRIP, displayMemory, drawingMemory, WS2811_GRB | WS2811_800kHz, NUM_STRIPS, {23, 22, 21, 20, 19});
byte PIN_LED_DATA[] = {23, 22, 21, 20, 19};
// Create OctoWS2811 object with custom pin configuration
const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(NUM_LEDS_PER_STRIP, displayMemory, drawingMemory, config, NUM_STRIPS, PIN_LED_DATA);
CRGB ledsBuffer[NUM_STRIPS][NUM_LEDS_PER_STRIP];

unsigned long lastUpdate = 0;

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

// Set the static IP to something other than INADDR_NONE (all zeros)
// to not use DHCP. The values here are just examples.
IPAddress staticIP{192, 168, 2, 117};
IPAddress subnetMask{255, 255, 255, 0};
IPAddress gateway{192, 168, 2, 1};
IPAddress broadcast{192, 168, 2, 255};

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

  // Initialize OctoWS2811
  leds.begin();
  leds.show();
  
  // Initialize FastLED
  // FastLED.addLeds<WS2811_PORTD, NUM_LEDS_PER_STRIP>(ledsBuffer, NUM_STRIPS * NUM_LEDS_PER_STRIP);
  // FastLED.addLeds<NUM_STRIPS, WS2813_800kHz, 19, GRB>(leds, NUM_LEDS_PER_STRIP);

  // Teensy's internal MAC retrieved
  uint8_t mac[6];
  Ethernet.macAddress(mac); // This is informative; it retrieves, not sets
  printf("MAC = %02x:%02x:%02x:%02x:%02x:%02x\r\n",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Add listeners
  // It's important to add these before doing anything with Ethernet so no events are missed.

  // Listen for link changes
  Ethernet.onLinkState([](bool state)
                       {
    if (state) {
      // Link is up
      digitalWrite(PIN_LED_STATUS, HIGH);  // Turn STATUS LED on
      printf("[Ethernet] Link ON\r\n");
    } else {
      // Link is down
      digitalWrite(PIN_LED_STATUS, LOW);   // Turn STATUS LED off
      printf("[Ethernet] Link OFF\r\n");
    } });

  // Listen for address changes
  Ethernet.onAddressChanged([]()
                            {
    IPAddress ip = Ethernet.localIP();
    bool hasIP = (ip != INADDR_NONE);
    if (hasIP) {
      printf("[Ethernet] Address changed:\r\n");

      printf("    Local IP = %u.%u.%u.%u\r\n", ip[0], ip[1], ip[2], ip[3]);
      ip = Ethernet.subnetMask();
      printf("    Subnet   = %u.%u.%u.%u\r\n", ip[0], ip[1], ip[2], ip[3]);
      ip = Ethernet.gatewayIP();
      printf("    Gateway  = %u.%u.%u.%u\r\n", ip[0], ip[1], ip[2], ip[3]);
      ip = Ethernet.dnsServerIP();
      if (ip != INADDR_NONE) {  // May happen with static IP
        printf("    DNS      = %u.%u.%u.%u\r\n", ip[0], ip[1], ip[2], ip[3]);
      }
    } else {
      printf("[Ethernet] Address changed: No IP address\r\n");
    } });

  if (initEthernet())
  {
    udp.begin(kServerPort);

    byte ipBytes[4];
    // Copy the IP address bytes into the ipBytes array
    for (int i = 0; i < 4; i++)
    {
      ipBytes[i] = staticIP[i];
    }
    // start ArtNet
    artnet.begin(mac, ipBytes);

    // this will be called for each packet received
    artnet.setArtDmxCallback(onDmxFrame);
  }
  digitalWrite(PIN_LED_STATUS, HIGH);
}

bool initEthernet()
{
  // DHCP
  if (staticIP == INADDR_NONE)
  {
    printf("Starting Ethernet with DHCP...\r\n");
    if (!Ethernet.begin())
    {
      printf("Failed to start Ethernet\r\n");
      return false;
    }

    // We can choose not to wait and rely on the listener to tell us when an address has been assigned
    if (kDHCPTimeout > 0)
    {
      printf("Waiting for IP address...\r\n");
      if (!Ethernet.waitForLocalIP(kDHCPTimeout))
      {
        printf("No IP address yet\r\n");
        // We may still get an address later, after the timeout, so continue instead of returning
      }
    }
  }
  else
  {
    // Static IP
    printf("Starting Ethernet with static IP...\r\n");
    if (!Ethernet.begin(staticIP, subnetMask, gateway))
    {
      printf("Failed to start Ethernet\r\n");
      return false;
    }

    // When setting a static IP, the address is changed immediately, but the link may not be up; optionally wait for the link here
    if (kLinkTimeout > 0)
    {
      printf("Waiting for link...\r\n");
      if (!Ethernet.waitForLink(kLinkTimeout))
      {
        printf("No link yet\r\n");
        // We may still see a link later, after the timeout, so continue instead of returning
      }
    }
  }
  return true;
}

// --------------------------------------------------------------------------
//  Main Program
// --------------------------------------------------------------------------
void loop() {
  unsigned long currentTime = millis();
    if (currentTime - lastUpdate >= updateInterval) {
        updateLEDs();
        lastUpdate = currentTime;
    }

    // Handle ArtNet data
    uint16_t packetType = artnet.read();
    if (packetType == ART_DMX) {
        digitalWrite(PIN_LED_DMX, HIGH);
        // delay(1);
        digitalWrite(PIN_LED_DMX, LOW);
    }
}

// --------------------------------------------------------------------------
//  Functions
// --------------------------------------------------------------------------
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t *data, IPAddress remoteIP) {
    int stripIndex = (universe - startUniverse) / universes_by_out;
    if (stripIndex < 0 || stripIndex >= NUM_STRIPS) {
        return; // Out of range
    }

    int ledOffset = (universe % universes_by_out) * (512 / CHANNELS_PER_LED);
    for (int i = 0; i < min(length / CHANNELS_PER_LED, NUM_LEDS_PER_STRIP); i++) {
        int actualLedIndex = ledOffset + i;
        leds.setPixel(stripIndex * NUM_LEDS_PER_STRIP + actualLedIndex,
                      data[i * CHANNELS_PER_LED],
                      data[i * CHANNELS_PER_LED + 1],
                      data[i * CHANNELS_PER_LED + 2]);
    }
}

void updateLEDs() {
    leds.show();
}