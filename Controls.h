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

// Pump and valve outputs (active HIGH — low-side transistors, 5V load)
void setPump(bool on);
void setValve(bool on);

// Brew PID API (tunings only — timing handled by state machine constants)
void setBrewPIDActive(bool active);  // Switch between heating and brew PID tunings
void setBrewPIDTunings(double kp, double ki, double kd);
void getBrewPIDTunings(double &kp, double &ki, double &kd);
void resetBrewPIDToDefaults();
void saveBrewSettingsToStorage();

// Persistent Storage Functions
void loadPIDFromStorage();
void savePIDToStorage();
void resetPIDToDefaults();

#endif