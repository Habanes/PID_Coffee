#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>

// Simple web server functions
void setupWebServer();
void handleWebServer();
String getIPAddress(); // Get current IP address for display

#endif
