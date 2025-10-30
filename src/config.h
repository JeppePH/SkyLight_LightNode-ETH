#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <SD.h> 
#include <QNEthernet.h>
using namespace qindesign::network;

// Configuration variables
extern IPAddress baseIP;
extern IPAddress subnetMask;
extern IPAddress gateway;
extern IPAddress broadcastIP;
extern IPAddress staticIP;
extern String ledType;
extern String colorOrder;
extern uint16_t updateSpeed;
extern const int chipSelect;  // Add this line
extern uint8_t mac[6];



// Function prototypes
void saveSettingsToSD();
void loadSettingsFromSD();
String ipToString(IPAddress ip);
bool stringToIP(String str, IPAddress &ip);

// Unique MAC setup 
void initDeviceMAC();
bool parseMAC(const String &s, uint8_t out[6]);
String macToString(const uint8_t m[6]);

#endif // CONFIG_H
