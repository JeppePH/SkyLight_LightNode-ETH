#include "artnet.h"

// ---------- Ctor ----------
Artnet::Artnet()
    : broadcastIP(255, 255, 255, 255),
      packetSize(0),
      lastOpcode(0),
      lastSequence(0),
      lastUniverse(0),
      lastLength(0) {}

// ---------- Public: begin ----------
void Artnet::begin()
{
    bool ok = Udp.begin(ARTNET_PORT);
    Serial.printf("[ArtNet] UDP %s on %u\n", ok ? "listening" : "FAILED", (unsigned)ARTNET_PORT);

    // Compute default broadcast from current IP/mask if available
    IPAddress ip = Ethernet.localIP();
    IPAddress mask = Ethernet.subnetMask();
    if (mask != IPAddress(0, 0, 0, 0))
    {
        uint32_t ip32 = (uint32_t)ip;
        uint32_t mask32 = (uint32_t)mask;
        uint32_t bc32 = (ip32 & mask32) | (~mask32);
        broadcastIP = IPAddress(bc32);
    }
}

// Optional legacy: static IP helper (not recommended if Ethernet already started)
void Artnet::begin(uint8_t mac[], uint8_t ip[])
{
    Ethernet.begin(mac, IPAddress(ip[0], ip[1], ip[2], ip[3]));
    Udp.begin(ARTNET_PORT);

    IPAddress local = Ethernet.localIP();
    IPAddress mask = Ethernet.subnetMask();
    if (mask == IPAddress(0, 0, 0, 0))
    {
        // Guess a mask if not provided
        uint8_t o0 = local[0];
        if (o0 >= 1 && o0 <= 126)
            mask = IPAddress(255, 0, 0, 0);
        else if (o0 >= 128 && o0 <= 191)
            mask = IPAddress(255, 255, 0, 0);
        else
            mask = IPAddress(255, 255, 255, 0);
    }
    uint32_t ip32 = (uint32_t)local;
    uint32_t mask32 = (uint32_t)mask;
    uint32_t bc32 = (ip32 & mask32) | (~mask32);
    broadcastIP = IPAddress(bc32);
}

// ---------- Public: network helpers ----------
void Artnet::setBroadcastAuto(IPAddress ip, IPAddress subnet)
{
    uint32_t ip32 = (uint32_t)ip;
    uint32_t sn32 = (uint32_t)subnet;
    uint32_t bc32 = (ip32 & sn32) | (~sn32);
    broadcastIP = IPAddress(bc32);
}
void Artnet::setBroadcast(IPAddress bc)
{
    broadcastIP = bc;
}
void Artnet::setBroadcast(uint8_t bc[4])
{
    broadcastIP = IPAddress(bc[0], bc[1], bc[2], bc[3]);
}

// ---------- Public: identity ----------
void Artnet::setShortName(const char *s)
{
    if (!s)
        return;
    strncpy(shortName, s, 17);
    shortName[17] = '\0';
}
void Artnet::setLongName(const char *s)
{
    if (!s)
        return;
    strncpy(longName, s, 63);
    longName[63] = '\0';
}

// ---------- Public: mapping ----------
void Artnet::setStartUniverse(uint16_t u) { startUniverse = u; }
void Artnet::setUniversesPerOutput(uint8_t n) { upo = (n < 1) ? 1 : n; }

// OUTi handles [start + i*UPO .. start + i*UPO + UPO-1]
int8_t Artnet::universeToPortIndex(uint16_t universe) const
{
    for (uint8_t i = 0; i < kNumPhysicalOutputs; ++i)
    {
        uint16_t base = startUniverse + i * upo;
        if (universe >= base && universe < (base + upo))
            return (int8_t)i;
    }
    return -1;
}

// ---------- Public: read/process ----------
uint16_t Artnet::read()
{
    packetSize = Udp.parsePacket();
    if (packetSize == 0)
        return 0;

    if (packetSize > ARTNET_MAX_BUFFER)
    {
        // Drain to keep socket healthy
        Udp.read(packetBuffer, ARTNET_MAX_BUFFER);
        return 0;
    }

    Udp.read(packetBuffer, packetSize);
    remoteIP = Udp.remoteIP();
    remotePort = Udp.remotePort();

    // Validate 8-byte ID ("Art-Net\0")
    if (memcmp(packetBuffer, "Art-Net", 7) != 0 || packetBuffer[7] != 0x00)
    {
        return 0;
    }

    // OpCode (LE)
    uint16_t opcode = packetBuffer[8] | (packetBuffer[9] << 8);
    lastOpcode = opcode;

    if (opcode == ART_DMX)
    {
        // Seq @12, SubUni @14, Net @15
        lastSequence = packetBuffer[12];

        uint8_t uniLow = packetBuffer[14];
        uint8_t uniHigh = packetBuffer[15]; // 7-bit net in hi
        uint16_t universe = ((uniHigh & 0x7F) << 8) | uniLow;
        lastUniverse = universe;

        // Length (BE)
        uint16_t length = (uint16_t(packetBuffer[16]) << 8) | packetBuffer[17];
        if (length > 512)
            length = 512;
        if (18 + length > packetSize)
            return 0; // malformed
        lastLength = length;

        // Mark port as "had data" if this universe belongs to any OUTi
        int8_t port = universeToPortIndex(universe);
        if (port >= 0)
            portHadData[(uint8_t)port] = true;

        if (artDmxCallback)
        {
            artDmxCallback(universe, length, lastSequence, packetBuffer + 18, remoteIP);
        }
        return ART_DMX;
    }
    else if (opcode == ART_POLL)
    {
        Serial.print("[ArtNet] ArtPoll from ");
        Serial.println(remoteIP);
        sendPollReply(remoteIP);
        return ART_POLL;
    }
    else if (opcode == ART_SYNC)
    {
        if (artSyncCallback)
            artSyncCallback(remoteIP);
        return ART_SYNC;
    }

    return 0; // unsupported
}

// ---------- Private: ArtPollReply ----------
void Artnet::sendPollReply(IPAddress pollerIP) {
  // We must unicast to controller (poll sender). Spec forbids broadcast for PollReply.
  IPAddress localIP = Ethernet.localIP();
  uint8_t mac6[6];
  Ethernet.macAddress(mac6);

//   const char *shortName = shortName // from your .h (setter)
//   const char *longName  = longName;  // from your .h (setter)

  // We have 8 logical outputs -> two replies of max 4 ports each
  const uint8_t totalPorts = 8;
  uint8_t portsSent = 0;
  uint8_t bindIndex = 1;

  while (portsSent < totalPorts) {
    const uint8_t portsThis = (uint8_t)min<uint8_t>(4, totalPorts - portsSent);

    // ArtPollReply minimum length is 207 bytes (spec); we’ll build full block incl. fillers.
    // We’ll zero-init everything so omitted fields are clean.
    uint8_t p[239] = {0};

    // 1) ID[8]
    memcpy(&p[0], "Art-Net", 7);
    p[7] = 0x00;

    // 2) OpCode (OpPollReply = 0x2100, little-endian)
    p[8]  = 0x00;
    p[9]  = 0x21;

    // 3) IP Address[4] (MSB first in array entries)
    p[10] = localIP[0];
    p[11] = localIP[1];
    p[12] = localIP[2];
    p[13] = localIP[3];

    // 4) Port (always 0x1936), little-endian
    p[14] = 0x36;
    p[15] = 0x19;

    // 5–6) VersInfoH/L (Node firmware version – your choice)
    p[16] = 0x01; // 1.0
    p[17] = 0x00;

    // 7) NetSwitch (bits 14..8 of first port’s 15-bit Port-Address)
    // 8) SubSwitch (bits 7..4 of first port’s Port-Address)
    // We map universes linearly starting at startUniverse_ across outputs.
    const uint16_t firstPortPA =
        (uint16_t)(0) + portsSent; // first universe in this reply
    p[18] = (uint8_t)((firstPortPA >> 8) & 0x7F);          // NetSwitch
    p[19] = (uint8_t)((firstPortPA >> 4) & 0x0F);          // SubSwitch (low nibble used)

    // 9–10) OEM (use 0xFFFF for dev)
    p[20] = 0xFF; // OemHi
    p[21] = 0xFF; // OemLo

    // 11) UBEA Version (0 = none)
    p[22] = 0x00;

    // 12) Status1 (bits per spec). Indicators normal (11), Port addr authority front panel (01), ROM boot=0, RDM=0, UBEA present=0
    p[23] = 0b11010000; // 0xD0

    // 13–14) ESTA manufacturer code (Lo, Hi). 0x0000 if unset.
    p[24] = 0x00; // EstaManLo
    p[25] = 0x00; // EstaManHi

    // 15) ShortName[18] (17 chars + null)
    strncpy((char*)&p[26], shortName ? shortName : "SkyLED Node", 17);
    p[26 + 17] = '\0';

    // 16) LongName[64]
    strncpy((char*)&p[44], longName ? longName : "SkyLED Teensy4.1 (8x Art-Net outputs)", 63);
    p[44 + 63] = '\0';

    // 17) NodeReport[64] – “#xxxx [yyyy] zzz…”
    static uint16_t bootCounter = 0;
    bootCounter = (bootCounter + 1) % 10000;
    char nodeReport[64];
    snprintf(nodeReport, sizeof(nodeReport), "#0001 [%04u] OK", bootCounter);
    memcpy(&p[108], nodeReport, strnlen(nodeReport, 63) + 1);

    // 18–19) NumPortsHi/Lo  (max 4 per reply)
    p[172] = 0x00;            // Hi
    p[173] = portsThis;       // Lo

    // 20) PortTypes[4] – set outputs as Art-Net->DMX (bit7=1), DMX512 (type 0)
    for (uint8_t i = 0; i < 4; ++i) {
      p[174 + i] = (i < portsThis) ? 0x80 : 0x00;
    }

    // 21) GoodInput[4] – inputs disabled (bit3=1)
    for (uint8_t i = 0; i < 4; ++i) {
      p[178 + i] = (i < portsThis) ? 0x08 : 0x00;
    }

    // 22) GoodOutputA[4] – set bit7 if we’ve output on that port (optional), else 0
    for (uint8_t i = 0; i < 4; ++i) {
      p[182 + i] = 0x00; // simple & safe; we set bit7 later once we stream
    }

    // 23) SwIn[4]  – low nibble of input universes (none)
    for (uint8_t i = 0; i < 4; ++i) p[186 + i] = 0x00;

    // 24) SwOut[4] – low nibble of output universes for this page
    for (uint8_t i = 0; i < 4; ++i) {
      if (i < portsThis) {
        const uint16_t pa = (uint16_t)0 + portsSent + i;
        p[190 + i] = (uint8_t)(pa & 0x0F);
      } else {
        p[190 + i] = 0x00;
      }
    }

    // 25) AcnPriority (sACN convert; not used) – set minimal sane value
    p[194] = 0x64; // 100

    // 26) SwMacro, 27) SwRemote – 0
    p[195] = 0x00;
    p[196] = 0x00;

    // 28–30) Spare – 0
    // 31) Style – StNode (0x00)
    p[200] = 0x00;

    // 32–37) MAC
    p[201] = mac6[0];
    p[202] = mac6[1];
    p[203] = mac6[2];
    p[204] = mac6[3];
    p[205] = mac6[4];
    p[206] = mac6[5];

    // 38) BindIP[4] – root device IP (same as local)
    p[207] = localIP[0];
    p[208] = localIP[1];
    p[209] = localIP[2];
    p[210] = localIP[3];

    // 39) BindIndex – 1 for root, >1 for subsequent pages
    p[211] = bindIndex;

    // 40) Status2 – set bit3 (15-bit addressing), bit0 web config if you like
    // bit3=1 -> supports 15-bit port address (Art-Net 3/4)
    p[212] = 0x08; // conservative: only bit3

    // 41) GoodOutputB[4] – leave 0
    // 42) Status3 – leave 0 (failsafe=hold, etc.)
    // 43–48) DefaultRespUID – 0
    // 49–51) UserHi/UserLo/RefreshRateHi – 0
    // 52) RefreshRateLo – 0 (unknown / not stated)
    // 53) BackgroundQueuePolicy – 0
    // 54) Filler[10] – 0

    // Ship it (unicast to poller).
    Udp.beginPacket(pollerIP, ARTNET_PORT);
    Udp.write(p, sizeof(p));
    Udp.endPacket();

    // Debug: dump we what sent (first 224+ bytes is fine)
    // dumpHex(p, sizeof(p));

    portsSent += portsThis;
    bindIndex++;
  }
}

// ---------- Debug ----------
void Artnet::printPacketHeader()
{
    Serial.print(F("Packet size="));
    Serial.print(packetSize);
    Serial.print(F(" bytes, OpCode=0x"));
    Serial.print(lastOpcode, HEX);
    Serial.print(F(", From "));
    Serial.println(remoteIP);
    if (lastOpcode == ART_DMX)
    {
        Serial.print(F("  Universe="));
        Serial.print(lastUniverse);
        Serial.print(F(", Length="));
        Serial.print(lastLength);
        Serial.print(F(", Seq="));
        Serial.println(lastSequence);
    }
    else if (lastOpcode == ART_POLL)
    {
        Serial.println(F("  (ArtPoll)"));
    }
    else if (lastOpcode == ART_SYNC)
    {
        Serial.println(F("  (ArtSync)"));
    }
}

void Artnet::printPacketContent()
{
    Serial.println(F("Packet content (first 32 bytes):"));
    uint16_t n = (packetSize < 32) ? packetSize : 32;
    for (uint16_t i = 0; i < n; ++i)
    {
        if (packetBuffer[i] < 16)
            Serial.print('0');
        Serial.print(packetBuffer[i], HEX);
        Serial.print(' ');
    }
    if (packetSize > n)
        Serial.print(F("..."));
    Serial.println();
}

static void dumpHex(const uint8_t *buf, size_t len) {
  Serial.printf("[ArtNet] TX %u bytes:\n", (unsigned)len);
  for (size_t i = 0; i < len; ++i) {
    if ((i % 16) == 0) Serial.printf("%04u: ", (unsigned)i);
    Serial.printf("%02X ", buf[i]);
    if ((i % 16) == 15) Serial.println();
  }
  if ((len % 16) != 0) Serial.println();
}