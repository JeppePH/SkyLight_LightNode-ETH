#include "config.h"

// Define configuration variables
IPAddress staticIP(192, 168, 1, 116);
IPAddress subnetMask(255, 255, 255, 0);
IPAddress gateway(192, 168, 1, 1);
IPAddress broadcastIP(192, 168, 1, 255);
String ledType = "WS2813";
String colorOrder = "GRB";
uint16_t updateSpeed = 60; // Hz

uint8_t mac[6] = { 0x04, 0xE9, 0xE5, 0x00, 0x00, 0x02 };  // Define mac here

void saveSettingsToSD()
{
    File file = SD.open("config.txt", FILE_WRITE);
    if (file)
    {
        file.println(ipToString(staticIP));
        file.println(ipToString(subnetMask));
        file.println(ipToString(gateway));
        file.println(ledType);
        file.println(colorOrder);
        file.println(updateSpeed);
        file.close();
        Serial.println("Settings saved to SD card.");
    }
    else
    {
        Serial.println("Failed to open config.txt for writing.");
    }
}

void loadSettingsFromSD()
{
    File file = SD.open("config.txt");
    if (file)
    {
        String line;
        if (file.available())
        {
            line = file.readStringUntil('\n');
            line.trim();
            stringToIP(line, staticIP);
        }
        if (file.available())
        {
            line = file.readStringUntil('\n');
            line.trim();
            stringToIP(line, subnetMask);
        }
        if (file.available())
        {
            line = file.readStringUntil('\n');
            line.trim();
            stringToIP(line, gateway);
        }
        if (file.available())
        {
            ledType = file.readStringUntil('\n');
            ledType.trim();
        }
        if (file.available())
        {
            colorOrder = file.readStringUntil('\n');
            colorOrder.trim();
        }
        if (file.available())
        {
            line = file.readStringUntil('\n');
            line.trim();
            updateSpeed = line.toInt();
        }
        file.close();
        Serial.println("Settings loaded from SD card.");
    }
    else
    {
        Serial.println("No config.txt found on SD card. Using default settings.");
    }
}

String ipToString(IPAddress ip)
{
    return String(ip[0]) + "." +
           String(ip[1]) + "." +
           String(ip[2]) + "." +
           String(ip[3]);
}

bool stringToIP(String str, IPAddress &ip)
{
    int parts[4];
    int part = 0;
    int start = 0;
    for (int i = 0; i <= str.length(); i++)
    {
        if (str.charAt(i) == '.' || i == str.length())
        {
            String sub = str.substring(start, i);
            parts[part++] = sub.toInt();
            start = i + 1;
            if (part > 3)
                break;
        }
    }
    if (part != 4)
        return false;

    ip = IPAddress(parts[0], parts[1], parts[2], parts[3]);
    return true;
}
