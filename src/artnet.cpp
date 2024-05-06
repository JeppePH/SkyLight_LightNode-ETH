// https://github.com/natcl/Artnet/tree/master

#include "artnet.h"

Artnet::Artnet() {}

void Artnet::begin(byte mac[], byte ip[])
{
  #if !defined(ARDUINO_SAMD_ZERO) && !defined(ESP8266) && !defined(ESP32)
    Ethernet.begin(mac,ip);
  #endif

  Udp.begin(ART_NET_PORT);
}

// void Artnet::begin()
// {
//   Udp.begin(ART_NET_PORT);
// }

void Artnet::setBroadcastAuto(IPAddress ip, IPAddress sn)
{
  //Cast in uint 32 to use bitwise operation of DWORD
  uint32_t ip32 = ip;
  uint32_t sn32 = sn;

  //Find the broacast Address
  uint32_t bc = (ip32 & sn32) | (~sn32);

  //sets the broadcast address
  setBroadcast(IPAddress(bc));
}

void Artnet::setBroadcast(byte bc[])
{
  //sets the broadcast address
  broadcast = bc;
}
void Artnet::setBroadcast(IPAddress bc)
{
  //sets the broadcast address
  broadcast = bc;
}

uint16_t Artnet::read()
{
  packetSize = Udp.parsePacket();

  remoteIP = Udp.remoteIP();
  if (packetSize <= MAX_BUFFER_ARTNET && packetSize > 0)
  {
      Udp.read(artnetPacket, MAX_BUFFER_ARTNET);

      // Check that packetID is "Art-Net" else ignore
      for (byte i = 0 ; i < 8 ; i++)
      {
        if (artnetPacket[i] != ART_NET_ID[i])
          return 0;
      }

      opcode = artnetPacket[8] | artnetPacket[9] << 8;

      if (opcode == ART_DMX)
      {
        sequence = artnetPacket[12];
        incomingUniverse = artnetPacket[14] | artnetPacket[15] << 8;
        dmxDataLength = artnetPacket[17] | artnetPacket[16] << 8;

        if (artDmxCallback) (*artDmxCallback)(incomingUniverse, dmxDataLength, sequence, artnetPacket + ART_DMX_START, remoteIP);
        return ART_DMX;
      }
      if (opcode == ART_POLL)
      {
        //fill the reply struct, and then send it to the network's broadcast address
        Serial.print("POLL from ");
        Serial.print(remoteIP);
        Serial.print(" broadcast addr: ");
        Serial.println(broadcast);

        #if !defined(ARDUINO_SAMD_ZERO) && !defined(ESP8266) && !defined(ESP32)
          IPAddress local_ip = Ethernet.localIP();
        #else
          IPAddress local_ip = WiFi.localIP();
        #endif
        node_ip_address[0] = local_ip[0];
      	node_ip_address[1] = local_ip[1];
      	node_ip_address[2] = local_ip[2];
      	node_ip_address[3] = local_ip[3];

        sprintf((char *)id, "Art-Net");
        memcpy(ArtPollReply.id, id, sizeof(ArtPollReply.id));
        memcpy(ArtPollReply.ip, node_ip_address, sizeof(ArtPollReply.ip));

        ArtPollReply.opCode = ART_POLL_REPLY;
        ArtPollReply.port =  ART_NET_PORT;

        memset(ArtPollReply.goodinput,  0x08, 4);
        memset(ArtPollReply.goodoutput,  0x80, NUMBER_OF_OUTPUTS+1);
        memset(ArtPollReply.porttypes,  0xc0, 4);

        uint8_t shortname [18];
        uint8_t longname [64];
        sprintf((char *)shortname, "Light Node");
        sprintf((char *)longname, "Desorb Light Node");
        memcpy(ArtPollReply.shortname, shortname, sizeof(shortname));
        memcpy(ArtPollReply.longname, longname, sizeof(longname));

        ArtPollReply.etsaman[0] = 0;
        ArtPollReply.etsaman[1] = 0;
        ArtPollReply.verH       = 1;
        ArtPollReply.ver        = 0;
        ArtPollReply.subH       = 0;
        ArtPollReply.sub        = 0;
        ArtPollReply.oemH       = 0;
        ArtPollReply.oem        = 0xFF;
        ArtPollReply.ubea       = 0;
        ArtPollReply.status     = 0xd2;
        ArtPollReply.swvideo    = 0;
        ArtPollReply.swmacro    = 0;
        ArtPollReply.swremote   = 0;
        ArtPollReply.style      = 0;

        ArtPollReply.numbportsH = 0;
        ArtPollReply.numbports  = NUMBER_OF_OUTPUTS+1;
        ArtPollReply.status2    = 0x08;

        ArtPollReply.bindip[0] = node_ip_address[0];
        ArtPollReply.bindip[1] = node_ip_address[1];
        ArtPollReply.bindip[2] = node_ip_address[2];
        ArtPollReply.bindip[3] = node_ip_address[3];

        uint8_t swin[NUMBER_OF_OUTPUTS]  = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A};
        uint8_t swout[NUMBER_OF_OUTPUTS] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A};
        for(uint8_t i = 0; i < NUMBER_OF_OUTPUTS; i++)
        {
            ArtPollReply.swout[i] = swout[i];
            ArtPollReply.swin[i] = swin[i];
        }
        sprintf((char *)ArtPollReply.nodereport, "%i DMX output universes active.", NUMBER_OF_OUTPUTS);
        Udp.beginPacket(remoteIP, ART_NET_PORT);//send the packet to the specific address
        Udp.write((uint8_t *)&ArtPollReply, sizeof(ArtPollReply));
        Udp.endPacket();

        Serial.println(sizeof(ArtPollReply));

        return ART_POLL;
      }
      if (opcode == ART_SYNC)
      {
        if (artSyncCallback) (*artSyncCallback)(remoteIP);
        return ART_SYNC;
      }
  }
  else
  {
    return 0;
  }
  return 0;
}

void Artnet::printPacketHeader()
{
  Serial.print("packet size = ");
  Serial.print(packetSize);
  Serial.print("\topcode = ");
  Serial.print(opcode, HEX);
  Serial.print("\tuniverse number = ");
  Serial.print(incomingUniverse);
  Serial.print("\tdata length = ");
  Serial.print(dmxDataLength);
  Serial.print("\tsequence n0. = ");
  Serial.println(sequence);
}

void Artnet::printPacketContent()
{
  for (uint16_t i = ART_DMX_START ; i < dmxDataLength ; i++){
    Serial.print(artnetPacket[i], DEC);
    Serial.print("  ");
  }
  Serial.println('\n');
}