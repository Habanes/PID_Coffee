#ifndef DISPLAY_H
#define DISPLAY_H

// Process 8 - Display. Multiplexes the 4-digit 7-seg (SevSeg, common anode).
// Shows: IDLE -> the selected menu view; COFFEE -> elapsed shot time;
// STEAM/HOT_WATER -> temperature; ECO -> static "ECO"; ERROR -> a scrolling
// short word naming the fault. Read-only consumer of state/settings.
// See ../Processes.txt (8).

void setupDisplay();    // call once in UiTask init
void refreshDisplay();  // call EVERY UiTask cycle (multiplex needs high rate)

#endif // DISPLAY_H
