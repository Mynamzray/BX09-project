#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "lcd_bl_pwm_bsp.h"
#include "Stopwatch_Manager.h"

namespace BLE_Manager {
    void toggleBluetooth();
    extern uint16_t liveCurveBuffer[32];
    extern int liveCurveCount;
}

#ifdef __cplusplus
extern "C" {
#endif
    #ifndef LV_FONT_DECLARE
        #define LV_FONT_DECLARE(font_name) extern const lv_font_t font_name;
    #endif

    LV_FONT_DECLARE(lv_font_montserrat_24)
    LV_FONT_DECLARE(lv_font_montserrat_36)
    LV_FONT_DECLARE(lv_font_montserrat_48)
#ifdef __cplusplus
}
#endif

Preferences prefs;
uint16_t global_all_time_best = 0; 
uint16_t global_history[8] = {0};
int global_hist_count = 0;

// Stopwatch NVS Persistent Storage
uint32_t global_sw_longest_spin = 0;
uint32_t global_sw_history_time[8] = {0};
uint32_t global_sw_history_sp[8] = {0};
int global_sw_hist_count = 0;

namespace UI {
    volatile bool readyToDraw = false;
    volatile int requested_ble_state = 1;  
    static int current_rendered_state = -1; 
    
    static uint32_t go_shoot_timer = 0;
    static bool is_showing_go_shoot = false;

    // LVGL Objects
    lv_obj_t * scr;
    lv_obj_t * label_status;
    lv_obj_t * led_status;
    lv_obj_t * label_current_rpm;
    lv_obj_t * label_all_time_rpm;
    lv_obj_t * label_history;
    lv_obj_t * chart;
    lv_obj_t * label_battery; 
    lv_chart_series_t * chart_series;

    // Panel containers & Stopwatch widgets
    lv_obj_t * panel_normal;
    lv_obj_t * panel_stopwatch;
    lv_obj_t * sw_arc;
    lv_obj_t * sw_run_label;
    lv_obj_t * sw_hist_label;
    
    // Direct Labels
    lv_obj_t * sw_cur_sp_val;
    lv_obj_t * sw_longest_spin_val;
    lv_obj_t * sw_time_label;

    static int current_displayed_rpm = 0;
    static int current_displayed_sw_sp = 0;

    static void anim_text_update_cb(void * var, int32_t v) {
        lv_obj_t * label = (lv_obj_t *)var;
        lv_label_set_text_fmt(label, "%d SP", v); 
    }

    void updateBattery(int percentage, float voltage, bool isCharging) {
        if (!label_battery) return;

        int voltageTenths = (int)(voltage * 10.0f + 0.5f);
        static int lastPercentage = -1;
        static int lastVoltageTenths = -1;
        static bool lastCharging = false;
        if (percentage == lastPercentage && voltageTenths == lastVoltageTenths &&
            isCharging == lastCharging) {
            return;
        }
        lastPercentage = percentage;
        lastVoltageTenths = voltageTenths;
        lastCharging = isCharging;

        const char* symbol;
        if (isCharging) {
            symbol = LV_SYMBOL_CHARGE; 
            lv_obj_set_style_text_color(label_battery, lv_palette_main(LV_PALETTE_GREEN), 0); 
        } else {
            if (percentage >= 80) symbol = LV_SYMBOL_BATTERY_FULL;
            else if (percentage >= 60) symbol = LV_SYMBOL_BATTERY_3;
            else if (percentage >= 40) symbol = LV_SYMBOL_BATTERY_2;
            else if (percentage >= 20) symbol = LV_SYMBOL_BATTERY_1;
            else symbol = LV_SYMBOL_BATTERY_EMPTY;
            
            if (percentage <= 20) {
                lv_obj_set_style_text_color(label_battery, lv_palette_main(LV_PALETTE_RED), 0);
            } else {
                lv_obj_set_style_text_color(label_battery, lv_color_white(), 0);
            }
        }
        
        lv_label_set_text_fmt(label_battery, "%d%% (%d.%dV) %s", percentage,
              voltageTenths / 10, voltageTenths % 10, symbol);
    } 

    void updateCurrentRPM(int target_rpm) {
        if (!label_current_rpm) return;
        if (current_displayed_rpm == target_rpm) return;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, label_current_rpm);
        lv_anim_set_values(&a, current_displayed_rpm, target_rpm);
        lv_anim_set_time(&a, 500);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, anim_text_update_cb);
        lv_anim_start(&a);

        current_displayed_rpm = target_rpm;
    }    

    void updateStopwatchCurrentSP(int target_sp) {
        if (!sw_cur_sp_val) return;
        if (current_displayed_sw_sp == target_sp) return;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, sw_cur_sp_val);
        lv_anim_set_values(&a, current_displayed_sw_sp, target_sp);
        lv_anim_set_time(&a, 500);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, anim_text_update_cb);
        lv_anim_start(&a);

        current_displayed_sw_sp = target_sp;
    }

    void updateChartCurve(uint16_t* turnData, int turnCount) {
        if (!chart || !chart_series || turnCount <= 0) return;

        uint16_t maxRpmInChart = 0;
        for (int i = 0; i < turnCount; i++) {
            if (turnData[i] > maxRpmInChart && turnData[i] < 18000) {
                maxRpmInChart = turnData[i];
            }
        }
        maxRpmInChart = (uint16_t)(maxRpmInChart * 1.15);
        if (maxRpmInChart < 1000) maxRpmInChart = 1000;

        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, maxRpmInChart);
        lv_chart_set_point_count(chart, turnCount);

        for (int i = 0; i < turnCount; i++) {
            lv_chart_set_value_by_id(chart, chart_series, i, turnData[i]);
        }
        lv_chart_refresh(chart);
    }

    void updateOfficialData(uint16_t origSP, uint16_t* history, int histCount) {
        updateCurrentRPM(origSP);
        
        // Pass official launch SP to Stopwatch_Manager if active
        Stopwatch_Manager::updateSP(origSP);

        bool hasNewAllTimeBest = origSP > global_all_time_best;
        if (hasNewAllTimeBest) {
            global_all_time_best = origSP;
            lv_label_set_text_fmt(label_all_time_rpm, "%d SP", global_all_time_best);
        }

        global_hist_count = histCount;
        for (int i = 0; i < histCount; i++) {
            global_history[i] = history[i];
        }

        prefs.begin("bx09_store", false); 
        if (hasNewAllTimeBest) {
            prefs.putUInt("best_rpm", global_all_time_best);
        }
        prefs.putInt("hist_cnt", global_hist_count); 
        prefs.putBytes("hist_arr", global_history, sizeof(global_history)); 
        prefs.end();

        if (label_history) {
            String histText = "";
            for (int i = 0; i < histCount && i < 7; i++) {
                histText += String(i + 1) + ". " + String(history[i]) + " SP\n";
            }
            if (histCount == 0) {
                histText = "No Official Data";
            }
            lv_label_set_text(label_history, histText.c_str());
        }
    }

    void loadStopwatchNVS() {
        prefs.begin("sw_store", true);
        global_sw_longest_spin = prefs.getUInt("best_time", 0);
        global_sw_hist_count = prefs.getInt("sw_cnt", 0);
        if (global_sw_hist_count > 0) {
            prefs.getBytes("sw_time", global_sw_history_time, sizeof(global_sw_history_time));
            prefs.getBytes("sw_sp", global_sw_history_sp, sizeof(global_sw_history_sp));
            
            Stopwatch_Manager::runCount = global_sw_hist_count;
            for (int i = 0; i < global_sw_hist_count && i < 8; i++) {
                Stopwatch_Manager::runHistory[i] = global_sw_history_time[i];
                Stopwatch_Manager::runPeakSP[i]  = (uint16_t)global_sw_history_sp[i];
            }
        }
        prefs.end();
    }

    void saveStopwatchNVS() {
        prefs.begin("sw_store", false);
        prefs.putUInt("best_time", global_sw_longest_spin);
        prefs.putInt("sw_cnt", global_sw_hist_count);
        prefs.putBytes("sw_time", global_sw_history_time, sizeof(global_sw_history_time));
        prefs.putBytes("sw_sp", global_sw_history_sp, sizeof(global_sw_history_sp));
        prefs.end();
    }

    void init() {
        lvgl_port_init();
        lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

        scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(scr, lv_color_white(), LV_PART_MAIN);

        // Load NVS data on startup
        loadStopwatchNVS();

        // ── Shared status bar ──────────────────────────────────────────
        led_status = lv_led_create(scr);
        lv_obj_align(led_status, LV_ALIGN_TOP_LEFT, 20, 16);
        lv_obj_set_size(led_status, 14, 14);
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
        lv_led_on(led_status);

        label_status = lv_label_create(scr);
        lv_label_set_text(label_status, "Searching for BX-09");
        lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 42, 13);
        lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(label_status, &lv_font_montserrat_24, 0);

        label_battery = lv_label_create(scr);
        lv_obj_align(label_battery, LV_ALIGN_TOP_RIGHT, -15, 13);
        lv_obj_set_style_text_font(label_battery, &lv_font_montserrat_24, 0);
        lv_label_set_text(label_battery, "--% (--V) " LV_SYMBOL_BATTERY_FULL);

        // ── Normal mode panel ──────────────────────────────────────────
        panel_normal = lv_obj_create(scr);
        lv_obj_set_size(panel_normal, 820, 320);
        lv_obj_set_pos(panel_normal, 0, 0);
        lv_obj_set_style_bg_opa(panel_normal, 0, 0);
        lv_obj_set_style_border_width(panel_normal, 0, 0);
        lv_obj_set_style_pad_all(panel_normal, 0, 0);
        lv_obj_set_style_radius(panel_normal, 0, 0);
        lv_obj_clear_flag(panel_normal, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(panel_normal, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_set_style_text_color(panel_normal, lv_color_white(), 0);
        lv_obj_t * lbl_cur_title = lv_label_create(panel_normal);
        lv_label_set_text(lbl_cur_title, "CURRENT SP");
        lv_obj_align(lbl_cur_title, LV_ALIGN_TOP_LEFT, 20, 58);
        lv_obj_set_style_text_color(lbl_cur_title, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(lbl_cur_title, &lv_font_montserrat_24, 0);

        label_current_rpm = lv_label_create(panel_normal);
        lv_label_set_text(label_current_rpm, "0 SP");
        lv_obj_align(label_current_rpm, LV_ALIGN_TOP_LEFT, 20, 85);
        lv_obj_set_style_text_font(label_current_rpm, &lv_font_montserrat_48, 0);

        lv_obj_t * lbl_best_title = lv_label_create(panel_normal);
        lv_label_set_text(lbl_best_title, "ALL-TIME BEST");
        lv_obj_align(lbl_best_title, LV_ALIGN_TOP_LEFT, 20, 152);
        lv_obj_set_style_text_color(lbl_best_title, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_obj_set_style_text_font(lbl_best_title, &lv_font_montserrat_24, 0);

        label_all_time_rpm = lv_label_create(panel_normal);
        prefs.begin("bx09_store", true);
        global_all_time_best = prefs.getUInt("best_rpm", 0);
        global_hist_count = prefs.getInt("hist_cnt", 0);
        if (global_hist_count > 0) prefs.getBytes("hist_arr", global_history, sizeof(global_history));
        prefs.end();
        lv_label_set_text_fmt(label_all_time_rpm, "%d SP", global_all_time_best);
        lv_obj_align(label_all_time_rpm, LV_ALIGN_TOP_LEFT, 20, 179);
        lv_obj_set_style_text_font(label_all_time_rpm, &lv_font_montserrat_36, 0);

        lv_obj_t * lbl_hist_title = lv_label_create(panel_normal);
        lv_label_set_text(lbl_hist_title, "Recent History");
        lv_obj_align(lbl_hist_title, LV_ALIGN_TOP_LEFT, 325, 58);
        lv_obj_set_style_text_color(lbl_hist_title, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_font(lbl_hist_title, &lv_font_montserrat_24, 0);

        label_history = lv_label_create(panel_normal);
        {
            String histText = "";
            for (int i = 0; i < global_hist_count && i < 7; i++)
                histText += String(i + 1) + ". " + String(global_history[i]) + " SP\n";
            if (global_hist_count == 0)
                histText = "1. -\n2. -\n3. -\n4. -\n5. -\n6. -\n7. -";
            lv_label_set_text(label_history, histText.c_str());
        }
        lv_obj_align(label_history, LV_ALIGN_TOP_LEFT, 325, 85);
        lv_obj_set_style_text_line_space(label_history, 2, 0);
        lv_obj_set_style_text_font(label_history, &lv_font_montserrat_24, 0);

        chart = lv_chart_create(panel_normal);
        lv_obj_set_size(chart, 225, 245);
        lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 580, 58);
        lv_obj_set_style_bg_opa(chart, 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(chart, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        chart_series = lv_chart_add_series(chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
        lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);

        // ── Stopwatch Panel ─────────────────────────────────────────────
        panel_stopwatch = lv_obj_create(scr);
        lv_obj_set_size(panel_stopwatch, 820, 320);
        lv_obj_set_pos(panel_stopwatch, 0, 0);
        lv_obj_set_style_bg_opa(panel_stopwatch, 0, 0);
        lv_obj_set_style_border_width(panel_stopwatch, 0, 0);
        lv_obj_set_style_pad_all(panel_stopwatch, 0, 0);
        lv_obj_set_style_radius(panel_stopwatch, 0, 0);
        lv_obj_clear_flag(panel_stopwatch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(panel_stopwatch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(panel_stopwatch, lv_color_white(), 0);
        lv_obj_add_flag(panel_stopwatch, LV_OBJ_FLAG_HIDDEN);

        // Left Column — Current SP & Longest Spin Record
        lv_obj_t * sw_lbl_sp_title = lv_label_create(panel_stopwatch);
        lv_label_set_text(sw_lbl_sp_title, "CURRENT SP");
        lv_obj_align(sw_lbl_sp_title, LV_ALIGN_TOP_LEFT, 20, 58);
        lv_obj_set_style_text_color(sw_lbl_sp_title, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(sw_lbl_sp_title, &lv_font_montserrat_24, 0);

        sw_cur_sp_val = lv_label_create(panel_stopwatch);
        lv_label_set_text(sw_cur_sp_val, "0 SP");
        lv_obj_align(sw_cur_sp_val, LV_ALIGN_TOP_LEFT, 20, 90);
        lv_obj_set_style_text_font(sw_cur_sp_val, &lv_font_montserrat_48, 0);

        lv_obj_t * sw_lbl_longest_title = lv_label_create(panel_stopwatch);
        lv_label_set_text(sw_lbl_longest_title, "LONGEST SPIN");
        lv_obj_align(sw_lbl_longest_title, LV_ALIGN_TOP_LEFT, 20, 175);
        lv_obj_set_style_text_color(sw_lbl_longest_title, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_obj_set_style_text_font(sw_lbl_longest_title, &lv_font_montserrat_24, 0);

        sw_longest_spin_val = lv_label_create(panel_stopwatch);
        if (global_sw_longest_spin > 0) {
            uint32_t lm = global_sw_longest_spin / 60000;
            uint32_t ls = (global_sw_longest_spin % 60000) / 1000;
            lv_label_set_text_fmt(sw_longest_spin_val, "%02u:%02u", lm, ls);
        } else {
            lv_label_set_text(sw_longest_spin_val, "--:--");
        }
        lv_obj_align(sw_longest_spin_val, LV_ALIGN_TOP_LEFT, 20, 205);
        lv_obj_set_style_text_color(sw_longest_spin_val, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_obj_set_style_text_font(sw_longest_spin_val, &lv_font_montserrat_36, 0);

        // Middle Column — Instrument Arc & Hero Timer
        sw_run_label = lv_label_create(panel_stopwatch);
        lv_label_set_text(sw_run_label, "Run: 0");
        lv_obj_align(sw_run_label, LV_ALIGN_TOP_LEFT, 280, 58);
        lv_obj_set_style_text_color(sw_run_label, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(sw_run_label, &lv_font_montserrat_24, 0);

        sw_arc = lv_arc_create(panel_stopwatch);
        lv_obj_set_size(sw_arc, 200, 200);
        lv_obj_align(sw_arc, LV_ALIGN_TOP_LEFT, 275, 82);
        lv_arc_set_rotation(sw_arc, 270);
        lv_arc_set_bg_angles(sw_arc, 0, 360);
        lv_arc_set_range(sw_arc, 0, 360);
        lv_arc_set_value(sw_arc, 0);
        lv_arc_set_mode(sw_arc, LV_ARC_MODE_NORMAL);
        lv_obj_clear_flag(sw_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(sw_arc, 10, LV_PART_MAIN);
        lv_obj_set_style_arc_color(sw_arc, lv_color_hex(0x1C1C1E), LV_PART_MAIN);
        lv_obj_set_style_arc_width(sw_arc, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sw_arc, lv_color_hex(0x00E5FF), LV_PART_INDICATOR);
        lv_obj_set_style_width(sw_arc, 0, LV_PART_KNOB);
        lv_obj_set_style_height(sw_arc, 0, LV_PART_KNOB);
        lv_obj_set_style_pad_all(sw_arc, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(sw_arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(sw_arc, 0, LV_PART_KNOB);

        // Centered Hero Timer
        sw_time_label = lv_label_create(panel_stopwatch);
        lv_label_set_text(sw_time_label, "00:00");
        lv_obj_set_width(sw_time_label, 190);
        lv_obj_align(sw_time_label, LV_ALIGN_TOP_LEFT, 280, 162);
        lv_obj_set_style_text_align(sw_time_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(sw_time_label, &lv_font_montserrat_48, 0);

        // Right Column — Run History
        lv_obj_t * sw_lbl_hist_title = lv_label_create(panel_stopwatch);
        lv_label_set_text(sw_lbl_hist_title, "RUN HISTORY");
        lv_obj_align(sw_lbl_hist_title, LV_ALIGN_TOP_LEFT, 570, 58);
        lv_obj_set_style_text_color(sw_lbl_hist_title, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_font(sw_lbl_hist_title, &lv_font_montserrat_24, 0);

        sw_hist_label = lv_label_create(panel_stopwatch);
        lv_label_set_text(sw_hist_label, "—");
        lv_obj_align(sw_hist_label, LV_ALIGN_TOP_LEFT, 570, 82);
        lv_obj_set_style_text_font(sw_hist_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_line_space(sw_hist_label, 4, 0);

        // Populate history text on boot (NEWEST/LATEST RUN AT TOP)
        if (global_sw_hist_count > 0) {
            char histText[384];
            size_t pos = 0;
            int total = global_sw_hist_count;
            int displayNum = total;
            for (int i = total - 1; i >= 0 && displayNum > (total > 6 ? total - 6 : 0); i--) {
                uint32_t rm = global_sw_history_time[i] / 60000;
                uint32_t rs = (global_sw_history_time[i] % 60000) / 1000;
                uint32_t sp = global_sw_history_sp[i];
                pos += snprintf(histText + pos, sizeof(histText) - pos,
                                 "%d. %02u:%02u • %u SP\n", displayNum--, rm, rs, sp);
            }
            lv_label_set_text(sw_hist_label, histText);
        }
    } 

    void showLowBatteryScreen() {
        lv_obj_t * overlay = lv_obj_create(scr);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_border_width(overlay, 0, 0);

        lv_obj_t * icon = lv_label_create(overlay);
        lv_label_set_text(icon, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -35);

        lv_obj_t * msg = lv_label_create(overlay);
        lv_label_set_text(msg, "LOW BATTERY\nPLEASE CHARGE");
        lv_obj_set_style_text_color(msg, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, 35);

        lv_timer_handler();
    }

    void updateStatus(int state) {
        if (state >= 0 && state <= 3) {
            requested_ble_state = state;
        }
    }

    static void panel_anim_x_cb(void* obj, int32_t v) {
        lv_obj_set_x((lv_obj_t*)obj, v);
    }
    static void panel_slide_out_done_cb(lv_anim_t* a) {
        lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x((lv_obj_t*)a->var, 0);
    }
    static void _slide_panels(lv_obj_t* outPanel, lv_obj_t* inPanel, int dir) {
        lv_obj_set_x(inPanel, dir * 820);
        lv_obj_clear_flag(inPanel, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, panel_anim_x_cb);
        lv_anim_set_time(&a, 220);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_var(&a, inPanel);
        lv_anim_set_values(&a, dir * 820, 0);
        lv_anim_start(&a);
        lv_anim_set_var(&a, outPanel);
        lv_anim_set_values(&a, 0, -dir * 820);
        lv_anim_set_ready_cb(&a, panel_slide_out_done_cb);
        lv_anim_start(&a);
    }
    void enterStopwatchMode() {
        current_rendered_state = -1;
        _slide_panels(panel_normal, panel_stopwatch, 1);
    }
    void exitStopwatchMode() {
        _slide_panels(panel_stopwatch, panel_normal, -1);
    }

    void updateStopwatchDisplay() {
        int64_t ms  = Stopwatch_Manager::elapsedMs();
        int     min = (int)(ms / 60000);
        int     sec = (int)((ms % 60000) / 1000);

        static int lastMin = -1, lastSec = -1;
        if (min != lastMin || sec != lastSec) {
            lastMin = min;
            lastSec = sec;
            lv_label_set_text_fmt(sw_time_label, "%02d:%02d", min, sec);
        }

        // Animated Rolling Number for Current SP (Just like Normal Mode!)
        int curSP = (int)Stopwatch_Manager::currentSP;
        updateStopwatchCurrentSP(curSP);

        // Synchronize with Stopwatch_Manager run records & peak SP
        int total = Stopwatch_Manager::runCount;
        if (total > 0 && total <= 8) {
            for (int i = 0; i < total; i++) {
                global_sw_history_time[i] = Stopwatch_Manager::runHistory[i];
                
                // Read exact Peak SP saved by Stopwatch_Manager or live current SP
                uint32_t effectiveSP = (uint32_t)Stopwatch_Manager::runPeakSP[i];
                if (effectiveSP == 0 && (uint32_t)Stopwatch_Manager::peakSP > 0) {
                    effectiveSP = (uint32_t)Stopwatch_Manager::peakSP;
                }
                if (effectiveSP == 0 && curSP > 0) {
                    effectiveSP = (uint32_t)curSP;
                }
                
                if (effectiveSP > global_sw_history_sp[i]) {
                    global_sw_history_sp[i] = effectiveSP;
                    Stopwatch_Manager::runPeakSP[i] = (uint16_t)effectiveSP;
                }

                if (global_sw_history_time[i] > global_sw_longest_spin) {
                    global_sw_longest_spin = global_sw_history_time[i];
                }
            }
            global_sw_hist_count = total;
        }

        static int lastRunCount = -1;
        bool runCountChanged = (Stopwatch_Manager::runCount != lastRunCount);
        if (runCountChanged) {
            lastRunCount = Stopwatch_Manager::runCount;
            
            // Persist new run data to NVS Flash memory
            saveStopwatchNVS();

            char runBuf[16];
            snprintf(runBuf, sizeof(runBuf), "Run: %d", Stopwatch_Manager::runCount);
            lv_label_set_text(sw_run_label, runBuf);
        }

        // Live update of active run in Run History alongside the timer!
        if (total > 0) {
            char histText[384];
            size_t pos = 0;
            int displayNum = total;
            for (int i = total - 1; i >= 0 && displayNum > (total > 6 ? total - 6 : 0); i--) {
                uint32_t runTimeMs = global_sw_history_time[i];
                // Active run line updates live along with timer during RUNNING or ARMED state!
                if (i == total - 1 && (Stopwatch_Manager::state == Stopwatch_Manager::State::RUNNING || Stopwatch_Manager::state == Stopwatch_Manager::State::ARMED)) {
                    runTimeMs = (uint32_t)ms;
                }
                
                uint32_t rm = runTimeMs / 60000;
                uint32_t rs = (runTimeMs % 60000) / 1000;
                uint32_t sp = global_sw_history_sp[i];
                
                // Ensure active run displays live SP during launch
                if (i == total - 1 && (sp == 0 || Stopwatch_Manager::state == Stopwatch_Manager::State::RUNNING)) {
                    uint32_t activeSP = (uint32_t)Stopwatch_Manager::peakSP;
                    if (activeSP == 0) activeSP = (uint32_t)curSP;
                    if (activeSP > sp) {
                        sp = activeSP;
                        global_sw_history_sp[i] = activeSP;
                        Stopwatch_Manager::runPeakSP[i] = (uint16_t)activeSP;
                    }
                }

                pos += snprintf(histText + pos, sizeof(histText) - pos,
                                 "%d. %02u:%02u • %u SP\n", displayNum--, rm, rs, sp);
            }
            lv_label_set_text(sw_hist_label, histText);
        }

        // Update Longest Spin record display
        static uint32_t lastLongestMs = 0xFFFFFFFF;
        if (global_sw_longest_spin != lastLongestMs) {
            lastLongestMs = global_sw_longest_spin;
            if (global_sw_longest_spin > 0) {
                uint32_t lm = global_sw_longest_spin / 60000;
                uint32_t ls = (global_sw_longest_spin % 60000) / 1000;
                lv_label_set_text_fmt(sw_longest_spin_val, "%02u:%02u", lm, ls);
            } else {
                lv_label_set_text(sw_longest_spin_val, "--:--");
            }
        }

        // Continuous Apple Watch style sweep (0-360 deg per 60000 ms)
        int arcValue = (int)((ms % 60000) * 360 / 60000);
        static int lastArcValue = -1;
        if (arcValue != lastArcValue) {
            lastArcValue = arcValue;
            lv_arc_set_value(sw_arc, arcValue);
        }

        static Stopwatch_Manager::State lastRenderedState = Stopwatch_Manager::State::IDLE;
        if (Stopwatch_Manager::state != lastRenderedState) {
            lastRenderedState = Stopwatch_Manager::state;
            switch (Stopwatch_Manager::state) {
                case Stopwatch_Manager::State::ARMED:
                    lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREEN));
                    lv_label_set_text(label_status, "BEYBLADE READY");
                    break;
                case Stopwatch_Manager::State::RUNNING:
                    lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_CYAN));
                    lv_label_set_text(label_status, "STOPWATCH RUNNING");
                    break;
                case Stopwatch_Manager::State::STOPPED:
                    lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
                    lv_label_set_text(label_status, "RUN COMPLETE");
                    break;
                default:
                    lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREY));
                    lv_label_set_text(label_status, "STOPWATCH IDLE");
                    break;
            }
        }
    }

    void renderStatusUI(int state) { 
        if (state == 0) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
            lv_label_set_text(label_status, "Searching for BX-09");
        } else if (state == 1) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_AMBER)); 
            lv_label_set_text(label_status, "WAITING FOR BEY");
        } else if (state == 2) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREEN)); 
            lv_label_set_text(label_status, "BEYBLADE READY");
        } else if (state == 3) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREY)); 
            lv_label_set_text(label_status, "BLE - SYSTEM PAUSED");
        } else if (state == 4) { 
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_CYAN)); 
            lv_label_set_text(label_status, "GO SHOOT !!");
        }
    }

    void handleUpdate() {
        if (Stopwatch_Manager::isStopwatchMode) {
            // Live capture of launcher RPM into Stopwatch_Manager during active BLE updates
            if (BLE_Manager::liveCurveCount > 0) {
                uint16_t maxLiveRpm = 0;
                for (int i = 0; i < BLE_Manager::liveCurveCount; i++) {
                    if (BLE_Manager::liveCurveBuffer[i] > maxLiveRpm && BLE_Manager::liveCurveBuffer[i] < 18000) {
                        maxLiveRpm = BLE_Manager::liveCurveBuffer[i];
                    }
                }
                if (maxLiveRpm > 0) {
                    Stopwatch_Manager::updateSP(maxLiveRpm);
                }
            }

            static unsigned long lastSwFrameMs = 0;
            unsigned long now = millis();
            if (now - lastSwFrameMs >= 20) {
                lastSwFrameMs = now;
                updateStopwatchDisplay();
            }
            return;
        }
        if (readyToDraw) {
            readyToDraw = false; 
            
            if (BLE_Manager::liveCurveCount > 0) {
                updateChartCurve(BLE_Manager::liveCurveBuffer, BLE_Manager::liveCurveCount);
                BLE_Manager::liveCurveCount = 0;
            }
            
            requested_ble_state = 1; 
            is_showing_go_shoot = true;
            go_shoot_timer = millis();
            
            renderStatusUI(4);
            current_rendered_state = 4;
        }

        if (is_showing_go_shoot) {
            if (millis() - go_shoot_timer > 2000) {
                is_showing_go_shoot = false;
                current_rendered_state = -1; 
            }
        } else {
            if (current_rendered_state != requested_ble_state) {
                renderStatusUI(requested_ble_state);
                current_rendered_state = requested_ble_state;
            }
        }
    }
}