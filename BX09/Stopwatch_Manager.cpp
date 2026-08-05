#include "Stopwatch_Manager.h"
#include <Preferences.h>
#include <esp_timer.h>

namespace Stopwatch_Manager {
    volatile bool     isStopwatchMode = false;
    volatile State    state           = State::IDLE;
    volatile uint16_t currentSP       = 0;

    uint32_t runHistory[MAX_RUNS] = {0};
    uint16_t runSP[MAX_RUNS]      = {0};
    int      runCount             = 0;

    static int64_t startUs  = 0;
    static int64_t frozenMs = 0;

    static void _saveRun() {
        if (frozenMs < 500) return;  // discard ghost runs under 500ms
        
        if (runCount < MAX_RUNS) {
            runHistory[runCount] = (uint32_t)frozenMs;
            runSP[runCount]      = currentSP;
            runCount++;
        } else {
            // Shift older runs left (pushing out run 0)
            for (int i = 0; i < MAX_RUNS - 1; i++) {
                runHistory[i] = runHistory[i + 1];
                runSP[i]      = runSP[i + 1];
            }
            runHistory[MAX_RUNS - 1] = (uint32_t)frozenMs;
            runSP[MAX_RUNS - 1]      = currentSP;
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
            state     = State::IDLE;
            frozenMs  = 0;
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
        currentSP = 0;
        state     = State::ARMED;
        Serial.println("[秒錶] ● 陀螺就位 — 待發射");
    }

    void start() {
        if (!isStopwatchMode || state != State::ARMED) return;
        startUs  = esp_timer_get_time();
        frozenMs = 0;
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
    }

    void clearHistory() {
        runCount = 0;
        frozenMs = 0;
        currentSP = 0;
        memset(runHistory, 0, sizeof(runHistory));
        memset(runSP, 0, sizeof(runSP));

        Preferences prefs;
        prefs.begin("sw_store", false);
        prefs.clear();
        prefs.end();

        Serial.println("🗑️ [秒錶] 歷史紀錄與 NVS 快取已成功清除！");
    }
}