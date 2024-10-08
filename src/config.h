#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <QNEthernet.h>
#include <SD.h> // Add this line


// Configuration variables
extern IPAddress staticIP;
extern IPAddress subnetMask;
extern IPAddress gateway;
extern IPAddress broadcastIP;
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
