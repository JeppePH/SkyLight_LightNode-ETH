#include "config.h"

// Define configuration variables
IPAddress baseIP(2, 0, 0, 80);
IPAddress subnetMask(255, 0, 0, 0);
IPAddress gateway(2, 0, 0, 1);
IPAddress broadcastIP(2, 255, 255, 255);
IPAddress staticIP = baseIP; // Default to baseIP
String ledType = "WS2813";
String colorOrder = "GRB";
uint16_t updateSpeed = 60; // Hz

uint8_t mac[6] = { 0x04, 0xE9, 0xE5, 0x00, 0x00, 0x01 };  // Define mac here

void saveSettingsToSD()
{
    File file = SD.open("config.txt", FILE_WRITE);
    if (file) {
        // Key/value format — clear file first
        file.seek(0);
        file.truncate();

        file.print("BASE=");    file.println(ipToString(baseIP));
        file.print("SUBNET=");  file.println(ipToString(subnetMask));
        file.print("GATEWAY="); file.println(ipToString(gateway));
        file.print("LEDTYPE="); file.println(ledType);
        file.print("ORDER=");   file.println(colorOrder);
        file.print("HZ=");      file.println(updateSpeed);
        // optional: MAC, BROADCAST if you want to persist them later

        file.close();
        Serial.println("Settings saved to SD card.");
    } else {
        Serial.println("Failed to open config.txt for writing.");
    }
}

static bool parseKVLine(const String &line, String &key, String &val) {
    int eq = line.indexOf('=');
    if (eq <= 0) return false;
    key = line.substring(0, eq); key.trim();
    val = line.substring(eq + 1); val.trim();
    return true;
}

void loadSettingsFromSD()
{
    File file = SD.open("config.txt");
    if (!file) {
        Serial.println("No config.txt found on SD card. Using default settings.");
        return;
    }

    // Defaults before load (in case some keys are missing)
    IPAddress newBase = baseIP;
    IPAddress newSub  = subnetMask;
    IPAddress newGw   = gateway;
    String    newType = ledType;
    String    newOrd  = colorOrder;
    uint16_t  newHz   = updateSpeed;

    bool sawKV = false;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (!line.length()) continue;

        String k,v;
        if (parseKVLine(line, k, v)) {
            sawKV = true;
            if      (k == "BASE")    { stringToIP(v, newBase); }
            else if (k == "SUBNET")  { stringToIP(v, newSub); }
            else if (k == "GATEWAY") { stringToIP(v, newGw); }
            else if (k == "LEDTYPE") { newType = v; }
            else if (k == "ORDER")   { newOrd  = v; }
            else if (k == "HZ")      { newHz   = (uint16_t)v.toInt(); }
            // ignore unknown keys
        } else {
            // ---- Backward compat (old format: first 6 lines) ----
            // 1: staticIP (we used to store this; treat it as BASE going forward)
            // 2: subnet, 3: gateway, 4: ledType, 5: colorOrder, 6: updateSpeed
            // We’ll read them once in order if present.
            static int legacyIdx = 0;
            switch (legacyIdx) {
                case 0: stringToIP(line, newBase); break;
                case 1: stringToIP(line, newSub);  break;
                case 2: stringToIP(line, newGw);   break;
                case 3: newType = line;            break;
                case 4: newOrd  = line;            break;
                case 5: newHz   = line.toInt();    break;
                default: break;
            }
            legacyIdx++;
        }
    }
    file.close();

    baseIP     = newBase;
    subnetMask = newSub;
    gateway    = newGw;
    ledType    = newType;
    colorOrder = newOrd;
    updateSpeed= newHz;

    Serial.println(sawKV ? "Settings (KV) loaded from SD."
                         : "Settings (legacy) loaded from SD.");
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
