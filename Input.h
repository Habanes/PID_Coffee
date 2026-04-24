#ifndef INPUT_H
#define INPUT_H
#include <Arduino.h>

void setupInput();
void syncInputState(); // Called in the main loop to read the results

#endif