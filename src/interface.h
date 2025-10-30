#ifndef INTERFACE_H
#define INTERFACE_H

#include <Arduino.h>
#include <QNEthernet.h>
using namespace qindesign::network;

void stopWebServer();
void setupWebServer();
void handleWebServer();

#endif // INTERFACE_H
