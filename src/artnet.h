#ifndef ARTNET_H
#define ARTNET_H

#include <Arduino.h>
#include <QNEthernet.h>
using namespace qindesign::network;

// ---------- Art-Net constants ----------
#define ARTNET_PORT        6454
#define ART_POLL           0x2000  // LE
#define ART_POLL_REPLY     0x2100  // LE
#define ART_DMX            0x5000  // LE
#define ART_SYNC           0x5200  // LE

// Max packet size we accept (DMX=512 + header headroom)
#define ARTNET_MAX_BUFFER  530

// "Art-Net\0" (8 bytes)
static const char ART_NET_ID[8] = "Art-Net";

// ---------- Callbacks ----------
typedef void (*ArtDmxCallback)(uint16_t universe, uint16_t length, uint8_t sequence,
                               uint8_t *data, IPAddress remoteIP);
typedef void (*ArtSyncCallback)(IPAddress remoteIP);

// ---------- Class ----------
class Artnet {
public:
  Artnet();

  // Begin listening (Ethernet must already be up via QNEthernet)
  void begin();

  // Stop listening (call before re-initializing to avoid socket leak)
  void stop();

  // Optional legacy helper: also configure a static IP (not recommended)
  void begin(uint8_t mac[], uint8_t ip[]);

  // Network helpers
  void setBroadcastAuto(IPAddress ip, IPAddress subnet);
  void setBroadcast(IPAddress bc);
  void setBroadcast(uint8_t bc[4]);

  // Identity / discovery (names shown in controllers)
  void setShortName(const char *s);  // up to 17 chars
  void setLongName (const char *s);  // up to 63 chars

  // Universe mapping for advertised ports
  // - startUniverse: base universe for OUT0 (0-based typical)
  // - universesPerOutput: UPO >=1 (default 1). OUTi handles [base + i*UPO .. base + i*UPO + UPO-1]
  void setStartUniverse(uint16_t u);
  void setUniversesPerOutput(uint8_t n);

  // Process incoming Art-Net packet. Returns opcode or 0 if none.
  uint16_t read();

  // Debug helpers
  void printPacketHeader();
  void printPacketContent();

  // Callbacks
  inline void setArtDmxCallback(ArtDmxCallback cb) { artDmxCallback = cb; }
  inline void setArtSyncCallback(ArtSyncCallback cb) { artSyncCallback = cb; }

private:
  // UDP socket
  EthernetUDP Udp;

  // Network
  IPAddress broadcastIP;

  // RX state
  uint8_t   packetBuffer[ARTNET_MAX_BUFFER];
  uint16_t  packetSize;
  IPAddress remoteIP;
  uint16_t  remotePort = 0;

  // Last packet (debug)
  uint16_t lastOpcode;
  uint8_t  lastSequence;
  uint16_t lastUniverse;
  uint16_t lastLength;

  // Config / identity
  static constexpr uint8_t kNumPhysicalOutputs = 8; // always advertise 8 ports
  uint16_t startUniverse = 0; // base universe for OUT0
  uint8_t  upo = 1; // universes-per-output (>=1)
  char     shortName[18] = "SkyLED Node";
  char     longName[64]  = "SkyLED Node 8 x Art-Net Outputs by DESORB";

  // Tracking: has each physical output seen data since boot?
  bool portHadData[kNumPhysicalOutputs] = {false,false,false,false,false,false,false,false};

  // Callbacks
  ArtDmxCallback  artDmxCallback  = nullptr;
  ArtSyncCallback artSyncCallback = nullptr;

  // Helpers
  void  sendPollReply(IPAddress toIp);
  int8_t universeToPortIndex(uint16_t universe) const; // -1 if outside any port range
};

#endif // ARTNET_H
