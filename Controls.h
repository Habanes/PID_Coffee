#ifndef CONTROLS_H
#define CONTROLS_H
#include <Arduino.h>

#define RELAY_PIN 14
#define EMERGENCY_STOP_TEMP 100.0  // Maximum safe temperature (°C)

// Default PID Parameters - Heating Mode (Factory Defaults)
#define DEFAULT_KP 50.0
#define DEFAULT_KI 0.55
#define DEFAULT_KD 20.0
#define DEFAULT_TARGET_TEMP 93.0
#define DEFAULT_IMAX 200.0  // Max integral accumulator (ms, out of 1000ms window = 20% max duty from I-term)

// Default PID Parameters - Brew Mode
// Higher Kp/Kd to aggressively fight the temperature drop from cold water extraction.
// Lower Ki since the pre-boost pre-saturates the boiler; integral just corrects steady-state drift.
#define DEFAULT_BREW_KP 50.0
#define DEFAULT_BREW_KI 0.55
#define DEFAULT_BREW_KD 20.0
#define DEFAULT_BREW_DELAY_SECONDS 10  // Seconds with heater OFF before brew PID takes over

void setupControls();
void updatePID();
void emergencyStop();
void setRelayForceOff(bool forceOff);
bool isRelayForceOff();

// Heating PID API
void setPIDTunings(double kp, double ki, double kd);
void getPIDTunings(double &kp, double &ki, double &kd);
void setIMax(double imax);
double getIMax();
void setTargetTemp(double temp);
bool isEmergencyStopActive();
void resetPIDMemory();  // Zero integral accumulator without changing tunings

// Brew mode API
void setBrewMode(bool active);
bool isBrewModeActive();
bool isBrewDelayPhase();
void setBrewPIDTunings(double kp, double ki, double kd, int delaySeconds);
void getBrewPIDTunings(double &kp, double &ki, double &kd, int &delaySeconds);
void resetBrewPIDToDefaults();
void saveBrewSettingsToStorage();

// Persistent Storage Functions
void loadPIDFromStorage();
void savePIDToStorage();
void resetPIDToDefaults();

#endif