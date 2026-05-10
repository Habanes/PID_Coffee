#ifndef CONTROLS_H
#define CONTROLS_H
#include <Arduino.h>
#include "Config.h"

void setupControls();
void updatePID();
void emergencyStop();
void setRelayForceOff(bool forceOff);
bool isRelayForceOff();

// Heating PID API
void setPIDTunings(double kp, double ki, double kd);
void getPIDTunings(double &kp, double &ki, double &kd);
void setTargetTemp(double temp);
bool isEmergencyStopActive();
void resetPIDMemory();  // Zero integral accumulator without changing tunings

// Heater output mode — set by state machine, consumed by ISR
enum HeaterMode { HEATER_OFF, HEATER_FULL_ON, HEATER_PID };
void setHeaterOutput(HeaterMode mode);

// Pump and valve outputs (active LOW — low-side transistors, 5V load)
void setPump(bool on);
void setValve(bool on);

// Brew PID API (tunings only — timing handled at runtime via brew timing API)
void setBrewPIDActive(bool active);  // Switch between heating and brew PID tunings
void setBrewPIDTunings(double kp, double ki, double kd);
void getBrewPIDTunings(double &kp, double &ki, double &kd);
void resetBrewPIDToDefaults();
void saveBrewSettingsToStorage();

// Brew timing API (milliseconds — settable at runtime, persisted to NVS)
unsigned long getPreinfuseMaxMs();
unsigned long getBloomMs();
unsigned long getPreheatMs();
unsigned long getBrewMaxMs();
unsigned long getBrewPidMaxMs();
void setBrewTimings(unsigned long preinfuse, unsigned long bloom, unsigned long preheat, unsigned long brewMax, unsigned long brewPidMax);
void resetBrewTimingsToDefaults();
void saveBrewTimingsToStorage();

// Preinfuse target pressure (Bar — settable at runtime, persisted to NVS)
float getPreinfuseTargetBar();
void setPreinfuseTargetBar(float bar);

// Heater mode query for UI — returns the current ISR heater mode
HeaterMode getHeaterMode();

// Persistent Storage Functions
void loadPIDFromStorage();
void savePIDToStorage();
void resetPIDToDefaults();

#endif