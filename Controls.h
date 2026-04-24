#ifndef CONTROLS_H
#define CONTROLS_H
#include <Arduino.h>

#define RELAY_PIN 14
#define EMERGENCY_STOP_TEMP 150.0  // Maximum safe temperature (°C)

// Default PID Parameters - Heating Mode (Factory Defaults)
#define DEFAULT_KP 30.0
#define DEFAULT_KI 1.0
#define DEFAULT_KD 0.0
#define DEFAULT_TARGET_TEMP 20.0

// Default PID Parameters - Brew Mode
// Higher Kp/Kd to aggressively fight the temperature drop from cold water extraction.
// Lower Ki since the pre-boost pre-saturates the boiler; integral just corrects steady-state drift.
#define DEFAULT_BREW_KP 50.0
#define DEFAULT_BREW_KI 0.5
#define DEFAULT_BREW_KD 8.0
#define DEFAULT_BREW_BOOST_SECONDS 5  // Seconds at 100% duty before brew PID takes over

void setupControls();
void updatePID();
void emergencyStop();

// Heating PID API
void setPIDTunings(double kp, double ki, double kd);
void getPIDTunings(double &kp, double &ki, double &kd);
void setTargetTemp(double temp);
bool isEmergencyStopActive();

// Brew mode API
void setBrewMode(bool active);
bool isBrewModeActive();
bool isBrewBoostPhase();
void setBrewPIDTunings(double kp, double ki, double kd, int boostSeconds);
void getBrewPIDTunings(double &kp, double &ki, double &kd, int &boostSeconds);
void resetBrewPIDToDefaults();
void saveBrewSettingsToStorage();

// Persistent Storage Functions
void loadPIDFromStorage();
void savePIDToStorage();
void resetPIDToDefaults();

#endif