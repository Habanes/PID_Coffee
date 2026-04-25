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

// Brew mode API
void setBrewMode(bool active);
bool isBrewModeActive();
bool isBrewBoostPhase();  // True during the boost phase (heater at boost duty cycle)
bool isBrewDelayPhase();  // True during the delay phase (heater at delay duty cycle)
void setBrewPIDTunings(double kp, double ki, double kd, int boostSeconds, int delaySeconds, int boostDutyCycle, int delayDutyCycle);
void getBrewPIDTunings(double &kp, double &ki, double &kd, int &boostSeconds, int &delaySeconds, int &boostDutyCycle, int &delayDutyCycle);
void resetBrewPIDToDefaults();
void saveBrewSettingsToStorage();

// Persistent Storage Functions
void loadPIDFromStorage();
void savePIDToStorage();
void resetPIDToDefaults();

#endif