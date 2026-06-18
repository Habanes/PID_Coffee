#include "Pid.h"
#include "State.h"
#include "Settings.h"
#include "Config.h"
#include <PID_v1.h>

// PID working variables (the library holds pointers to these).
static double pidInput    = 0.0;    // currentTemperature
static double pidOutput   = 0.0;    // 0..SSR_WINDOW_MS (ON time)
static double pidSetpoint = 0.0;    // currentTargetTemperature

// 8-arg rancilio fork: arg7 = pMode (1 = P_ON_E), arg8 = DIRECT.
static PID myPID(&pidInput, &pidOutput, &pidSetpoint,
                 DEFAULT_HEATING_KP, DEFAULT_HEATING_KI, DEFAULT_HEATING_KD, 1, DIRECT);

static bool prevPid = false;        // was the heater in PID mode last cycle?

static void resetIntegral() {
    pidOutput = 0.0;                // PID_v1 seeds outputSum from output on resume
    myPID.SetMode(MANUAL);
    myPID.SetMode(AUTOMATIC);
}

void setupPid() {
    myPID.SetSampleTime(SSR_WINDOW_MS);
    myPID.SetOutputLimits(0, SSR_WINDOW_MS);
    myPID.SetIntegratorLimits(0, PID_IMAX);     // rancilio-fork anti-windup clamp
    myPID.SetSmoothingFactor(PID_EMA_FACTOR);   // rancilio-fork derivative EMA
    myPID.SetMode(AUTOMATIC);
    prevPid = false;
}

void computePid() {
    STATE_LOCK();
    HeaterMode     mode   = state.heaterMode;
    float          temp   = state.currentTemperature;
    float          target = state.currentTargetTemperature;
    MachineState   ms     = state.machineState;
    CoffeeSubstate cs     = state.coffeeSubstate;
    STATE_UNLOCK();

    bool wantPid = (mode == HEATER_PID);

    if (wantPid) {
        if (!prevPid) resetIntegral();          // entering PID -> clear windup

        // Brew gains only during the BREW_PID phase; heating gains otherwise.
        bool useBrew = (ms == STATE_COFFEE && cs == SUB_BREW_PID);
        SETTINGS_LOCK();
        double kp = useBrew ? settings.brewKp : settings.heatingKp;
        double ki = useBrew ? settings.brewKi : settings.heatingKi;
        double kd = useBrew ? settings.brewKd : settings.heatingKd;
        SETTINGS_UNLOCK();

        myPID.SetTunings(kp, ki, kd, 1);
        pidInput    = temp;
        pidSetpoint = target;                   // offset already in temp
        myPID.Compute();
    } else {
        pidOutput = 0.0;                        // ignored by ISR in non-PID modes
    }
    prevPid = wantPid;

    STATE_LOCK();
    state.heatingPidOutput = pidOutput;
    STATE_UNLOCK();
}
