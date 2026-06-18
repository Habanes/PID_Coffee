#ifndef BUZZER_H
#define BUZZER_H

// Process 11 - Buzzer. Non-blocking sound engine. Other tasks request one-shot
// sounds via buzzerPlay() (thread-safe queue); buzzerTick() sequences them on
// the UiTask. The ERROR siren is level-driven (passed into buzzerTick).
// See ../Processes.txt (11).

enum Sound {
    SND_TICK,        // encoder step
    SND_CLICK,       // button press
    SND_MODE_ENTER,  // entering COFFEE/STEAM - ascending perfect fifth
    SND_MODE_EXIT    // leaving to IDLE - descending
};

void buzzerInit();                  // create queue (call once at boot)
void buzzerStartupJingle();         // blocking, boot only (no tasks yet)
void buzzerPlay(Sound s);           // enqueue a one-shot (any task)
void buzzerTick(bool errorSiren);   // call frequently from UiTask

#endif // BUZZER_H
