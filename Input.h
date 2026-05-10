#ifndef INPUT_H
#define INPUT_H
#include <Arduino.h>
#include "Config.h"

void setupInput();
void syncInputState(); // Called from TaskControl (Core 1) every 100ms

#endif