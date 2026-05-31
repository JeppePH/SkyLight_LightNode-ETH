#include "config.h"

// Define configuration variables
IPAddress baseIP(2, 200, 0, 100);
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
        file.print("MAC=");     file.println(macToString(mac));

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
    int legacyIdx = 0;
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
            else if (k == "MAC")     { parseMAC(v, mac); }
            // ignore unknown keys
        } else {
            // ---- Backward compat (old format: first 6 lines) ----
            // 1: staticIP (we used to store this; treat it as BASE going forward)
            // 2: subnet, 3: gateway, 4: ledType, 5: colorOrder, 6: updateSpeed
            // We’ll read them once in order if present.
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

// =======================================================
//  Unique MAC address management
// =======================================================

// --- Helper functions ---
static bool isAll(const uint8_t *p, uint8_t v, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (p[i] != v) return false;
    return true;
}

String macToString(const uint8_t m[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

bool parseMAC(const String &s, uint8_t out[6]) {
    unsigned int b[6];
    if (sscanf(s.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; ++i) out[i] = (uint8_t)b[i];
        return true;
    }
    return false;
}

// --- Read Teensy 4.x fuse MAC (IMXRT OCOTP) ---
static bool readFuseMAC(uint8_t out[6]) {
    const uint32_t mac1 = *(volatile const uint32_t*)0x401F4410; // high 16 bits + OUI
    const uint32_t mac0 = *(volatile const uint32_t*)0x401F4414; // low 32 bits

    out[0] = (mac1 >> 8) & 0xFF;
    out[1] = mac1 & 0xFF;
    out[2] = (mac0 >> 24) & 0xFF;
    out[3] = (mac0 >> 16) & 0xFF;
    out[4] = (mac0 >> 8) & 0xFF;
    out[5] = mac0 & 0xFF;

    if (isAll(out, 0x00, 6) || isAll(out, 0xFF, 6))
        return false;  // unprogrammed

    out[0] &= 0xFE;    // ensure unicast
    return true;
}

// --- Read "MAC=" line from config.txt ---
static bool loadPersistedMAC(uint8_t out[6]) {
    File f = SD.open("config.txt");
    if (!f) return false;
    bool ok = false;
    while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        if (line.startsWith("MAC=")) {
            String val = line.substring(4);
            ok = parseMAC(val, out);
            break;
        }
    }
    f.close();
    return ok;
}

// --- Write or replace MAC= line in config.txt ---
static void persistMAC(const uint8_t m[6]) {
    String lines;
    if (SD.exists("config.txt")) {
        File r = SD.open("config.txt");
        while (r && r.available()) lines += r.readStringUntil('\n');
        if (r) r.close();
    }

    // Remove existing MAC= lines
    String outText;
    int start = 0;
    while (start < lines.length()) {
        int nl = lines.indexOf('\n', start);
        if (nl < 0) nl = lines.length();
        String L = lines.substring(start, nl); L.trim();
        if (!L.startsWith("MAC=")) {
            outText += L; outText += "\n";
        }
        start = nl + 1;
    }
    outText += "MAC=" + macToString(m) + "\n";

    File t = SD.open("config.tmp", FILE_WRITE);
    if (t) {
        t.print(outText);
        t.flush();
        t.close();
        SD.remove("config.txt");
        SD.rename("config.tmp", "config.txt");
    }
}

// --- Generate a new locally-administered address ---
static void generateLocalMAC(uint8_t out[6]) {
    // 0x02 → Locally-administered, unicast
    out[0] = 0x02;
    out[1] = 0xDE;
    out[2] = 0x5B;
    uint32_t salt = millis() ^ ((uint32_t)baseIP[3] << 8);
    out[3] = (salt >> 16) & 0xFF;
    out[4] = (salt >> 8) & 0xFF;
    out[5] = salt & 0xFF;
}

// --- Main initializer ---
void initDeviceMAC() {
    uint8_t temp[6];

    // 1) Try fuse MAC
    if (readFuseMAC(temp)) {
        memcpy(mac, temp, 6);
        Serial.print("Using fuse MAC: ");
        Serial.println(macToString(mac));
        return;
    }

    // 2) Try persisted MAC
    if (loadPersistedMAC(temp)) {
        memcpy(mac, temp, 6);
        Serial.print("Using persisted MAC: ");
        Serial.println(macToString(mac));
        return;
    }

    // 3) Generate new one and persist
    generateLocalMAC(temp);
    memcpy(mac, temp, 6);
    persistMAC(mac);
    Serial.print("Generated & persisted new MAC: ");
    Serial.println(macToString(mac));
}