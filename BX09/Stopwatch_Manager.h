#pragma once
#include <Arduino.h>

namespace Stopwatch_Manager {
    enum class State { IDLE, ARMED, RUNNING, STOPPED };

    extern volatile bool     isStopwatchMode;
    extern volatile State    state;
    extern volatile uint16_t currentSP;

    static const int MAX_RUNS = 8;
    extern uint32_t runHistory[MAX_RUNS];  // elapsed ms for completed runs
    extern uint16_t runSP[MAX_RUNS];       // launch SP for completed runs
    extern int      runCount;              // count of completed saved runs

    void    toggleMode();
    void    arm();          // beyblade installed → ready for launch
    void    start();        // 0x70 received → launched, start timer
    void    stop();         // freeze timer, save run
    void    updateSP(uint16_t sp);
    void    clearHistory(); // clear all saved runs and reset NVS
    int64_t elapsedMs();
}