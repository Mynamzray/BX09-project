#include "Stopwatch_Manager.h"
#include <esp_timer.h>

namespace Stopwatch_Manager {
    volatile bool     isStopwatchMode = false;
    volatile State    state           = State::IDLE;
    volatile uint16_t currentSP       = 0;
    volatile uint16_t peakSP          = 0;

    uint32_t runHistory[MAX_RUNS] = {};
    uint16_t runPeakSP[MAX_RUNS]  = {};
    int      runCount              = 0;

    static int64_t startUs  = 0;
    static int64_t frozenMs = 0;

    static void _saveRun() {
        if (frozenMs < 500) return;  // discard ghost runs under 500ms
        if (runCount < MAX_RUNS) {
            runHistory[runCount]  = (uint32_t)frozenMs;
            runPeakSP[runCount++] = peakSP;
        } else {
            for (int i = 0; i < MAX_RUNS - 1; i++) {
                runHistory[i] = runHistory[i + 1];
                runPeakSP[i]  = runPeakSP[i + 1];
            }
            runHistory[MAX_RUNS - 1] = (uint32_t)frozenMs;
            runPeakSP[MAX_RUNS - 1]  = peakSP;
        }
    }

    int64_t elapsedMs() {
        if (state == State::RUNNING)
            return (esp_timer_get_time() - startUs) / 1000;
        return frozenMs;
    }

    void toggleMode() {
        if (isStopwatchMode) {
            if (state == State::RUNNING) {
                frozenMs = (esp_timer_get_time() - startUs) / 1000;
                _saveRun();
            }
            state           = State::IDLE;
            isStopwatchMode = false;
        } else {
            isStopwatchMode = true;
            state    = State::IDLE;
            frozenMs = 0;
            peakSP   = 0;
            currentSP = 0;
        }
        Serial.printf("[秒錶] 模式: %s\n", isStopwatchMode ? "ON" : "OFF");
    }

    void arm() {
        if (!isStopwatchMode) return;
        if (state == State::RUNNING) {
            frozenMs = (esp_timer_get_time() - startUs) / 1000;
            _saveRun();
        }
        frozenMs  = 0;
        peakSP    = 0;
        currentSP = 0;
        state     = State::ARMED;
        Serial.println("[秒錶] ● 陀螺就位 — 待發射");
    }

    void start() {
        if (!isStopwatchMode || state != State::ARMED) return;
        startUs  = esp_timer_get_time();
        frozenMs = 0;
        peakSP   = 0;
        state    = State::RUNNING;
        Serial.println("[秒錶] ▶ 計時開始！");
    }

    void stop() {
        if (state != State::RUNNING) return;
        frozenMs = (esp_timer_get_time() - startUs) / 1000;
        _saveRun();
        state = State::STOPPED;
        Serial.printf("[秒錶] ■ 停止: %lldms\n", frozenMs);
    }

    void updateSP(uint16_t sp) {
        currentSP = sp;
        if (state == State::RUNNING && sp > peakSP) peakSP = sp;
    }
}
