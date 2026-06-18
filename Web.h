#ifndef WEB_H
#define WEB_H

// Process 9 - Web GUI. WiFi (station, AP fallback) + HTTP server on Core 0.
// Serves the frontend (style from NewWebsiteIdea/), exposes /api/state and
// /api/settings. Setting writes are accepted only while machineState == IDLE.
// See ../Processes.txt (9) and ../WebUI Style.txt.

#include <Arduino.h>

void startWebTask();   // create the WebTask (call once at boot)
String webGetIp();     // current IP as text (for the 7-seg IP view)

#endif // WEB_H
