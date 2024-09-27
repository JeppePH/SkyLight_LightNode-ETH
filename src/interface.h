#ifndef INTERFACE_H
#define INTERFACE_H

#include <Arduino.h>
#include <QNEthernet.h>

using namespace qindesign::network;

void setupWebServer();
void handleWebServer();
void serveConfigPage(EthernetClient &client);
void handleFormSubmission(String request, EthernetClient &client);

#endif // INTERFACE_H
