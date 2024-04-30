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
#include <FastLED.h>
#include <QNEthernet.h>

using namespace qindesign::network;

#if defined(__IMXRT1062__)
extern "C" uint32_t set_arm_clock(uint32_t frequency);
#endif

// --------------------------------------------------------------------------
//  Configuration
// --------------------------------------------------------------------------
// FastLED
#define NUM_LEDS_PER_STRIP 170
#define NUM_STRIPS 5
#define CHANNELS_PER_LED 3
#define PIN_LED_STATUS 35
#define PIN_LED_DMX 34
#define PIN_LED_POLL 33

const bool DEBUG = true;

// Data arrays and buffer
CRGB ledsStrip[NUM_STRIPS][NUM_LEDS_PER_STRIP];
CRGB ledsBuffer[NUM_STRIPS][NUM_LEDS_PER_STRIP];

const uint8_t PIN_LED_DATA[] = {23, 22, 21, 20, 19}; 

const int universes_by_out = 2;   // Adjust this as needed
const int startUniverse = 0;      // Starting universe number
const int maxUniverses = NUM_STRIPS * universes_by_out;

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

// --------------------------------------------------------------------------
//  Main Setup
// --------------------------------------------------------------------------
void setup()
{
  set_arm_clock(160000000); // 600 MHz max default // 160mhz just worked
  delay(2000);
  Serial.begin(115200);
  while (!Serial && millis() < 4000)
  {
    // Wait for Serial
  }
  printf("Starting...\r\n");

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_LED_DMX, OUTPUT);
  pinMode(PIN_LED_POLL, OUTPUT);

  // Initialize each LED strip
  FastLED.addLeds<WS2813, 23, GRB>(ledsStrip[0], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2813, 22, GRB>(ledsStrip[1], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2813, 21, GRB>(ledsStrip[2], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2813, 20, GRB>(ledsStrip[3], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2813, 19, GRB>(ledsStrip[4], NUM_LEDS_PER_STRIP);

  // Teensy's internal MAC retrieved
  uint8_t mac[6];
  Ethernet.macAddress(mac); // This is informative; it retrieves, not sets
  printf("MAC = %02x:%02x:%02x:%02x:%02x:%02x\r\n",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Add listeners
  // It's important to add these before doing anything with Ethernet so no events are missed.

  // Listen for link changes
  Ethernet.onLinkState([](bool state) {
    if (state) {
      // Link is up
      digitalWrite(PIN_LED_STATUS, HIGH);  // Turn STATUS LED on
      printf("[Ethernet] Link ON\r\n");
    } else {
      // Link is down
      digitalWrite(PIN_LED_STATUS, LOW);   // Turn STATUS LED off
      printf("[Ethernet] Link OFF\r\n");
    }
  });
                         
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
void loop()
{
  digitalWrite(PIN_LED_DMX, LOW);
  digitalWrite(PIN_LED_POLL, LOW);

  // Handle ArtNet date
  uint16_t r = artnet.read();
  if (r == ART_POLL)
  {
    digitalWrite(PIN_LED_POLL, HIGH);
    delay(100); // ToDo: Make a timerinterrupt to blink the led for a period
  }
  if (r == ART_DMX)
  {
    digitalWrite(PIN_LED_DMX, HIGH);
    // print out our data
    Serial.print("universe number = ");
    Serial.print(artnet.getUniverse());
    Serial.print("\tdata length = ");
    Serial.print(artnet.getLength());
    Serial.print("\tsequence n0. = ");
    Serial.println(artnet.getSequence());
    Serial.print("DMX data: ");
    for (int i = 0; i < artnet.getLength(); i++)
    {
      Serial.print(artnet.getDmxFrame()[i]);
      Serial.print("  ");
    }
    Serial.println();
  }
}

// --------------------------------------------------------------------------
//  Functions
// --------------------------------------------------------------------------
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t *data, IPAddress remoteIP)
{
    // Debug: Print the universe and calculated stripIndex
    Serial.print("Received universe: ");
    Serial.println(universe);

    int stripIndex = (universe - startUniverse) / universes_by_out;
    Serial.print("Mapped strip index: ");
    Serial.println(stripIndex);

    if (stripIndex < 0 || stripIndex >= NUM_STRIPS) {
        Serial.println("Error: Universe number out of expected strip range");
        return; // Universe number out of expected range
    }

    int ledOffset = (universe % universes_by_out) * (NUM_LEDS_PER_STRIP / universes_by_out);
    for (int i = 0; i < length / CHANNELS_PER_LED; i++) {
        int actualLedIndex = ledOffset + i;
        if (actualLedIndex < NUM_LEDS_PER_STRIP) {
            ledsBuffer[stripIndex][actualLedIndex].setRGB(
                data[i * CHANNELS_PER_LED],
                data[i * CHANNELS_PER_LED + 1],
                data[i * CHANNELS_PER_LED + 2]
            );
        }
    }

    memcpy(ledsStrip[stripIndex], ledsBuffer[stripIndex], sizeof(ledsStrip[0]));
    FastLED.show();
    Serial.println("LEDs updated.");
}