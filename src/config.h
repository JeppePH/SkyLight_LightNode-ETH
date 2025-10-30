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

#endif // CONFIG_H
