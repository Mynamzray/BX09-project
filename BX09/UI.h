#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "lcd_bl_pwm_bsp.h"

namespace BLE_Manager {
    void toggleBluetooth();
    // 🟢 引入 BLE 模組的轉速快取，準備在主執行緒繪圖
    extern uint16_t liveCurveBuffer[32];
    extern int liveCurveCount;
}

// ==========================================
// 🛠️ 跨筆電字體強行解鎖修復 (繞過 lv_conf.h)
// ==========================================
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

// ==========================================
// [模組 2] LVGL 渲染器 (橫向超寬儀表板版)
// ==========================================
namespace UI {
    volatile bool readyToDraw = false;

    volatile int requested_ble_state = 1;  
    static int current_rendered_state = -1; 
    
    static uint32_t go_shoot_timer = 0;
    static bool is_showing_go_shoot = false;

    // LVGL 物件指標
    lv_obj_t * scr;
    lv_obj_t * label_status;
    lv_obj_t * led_status;
    lv_obj_t * label_current_rpm;
    lv_obj_t * label_all_time_rpm;
    lv_obj_t * label_history;
    lv_obj_t * chart;
    lv_obj_t * label_battery; 
    lv_chart_series_t * chart_series;

    static int current_displayed_rpm = 0;

    static void anim_text_update_cb(void * var, int32_t v) {
        lv_obj_t * label = (lv_obj_t *)var;
        lv_label_set_text_fmt(label, "%d SP", v); 
    }

    void updateBattery(int percentage, float voltage, bool isCharging) {
        if (!label_battery) return;

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
        
        String vStr = String(voltage, 1);
        lv_label_set_text_fmt(label_battery, "%d%% (%sV) %s", percentage, vStr.c_str(), symbol);
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

    // 🟢 輕量化即時轉速曲線繪製器
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

        global_hist_count = histCount;
        for (int i = 0; i < histCount; i++) {
            global_history[i] = history[i];
        }

        prefs.begin("bx09_store", false); 
        prefs.putInt("hist_cnt", global_hist_count); 
        prefs.putBytes("hist_arr", global_history, sizeof(global_history)); 
        prefs.end();

        if (label_history) {
            String histText = "";
            for (int i = 0; i < histCount; i++) {
                histText += String(i + 1) + ". " + String(history[i]) + " SP\n";
            }
            if (histCount == 0) {
                histText = "No Official Data";
            }
            lv_label_set_text(label_history, histText.c_str());
        }
    }

    void init() {
        lvgl_port_init();
        lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

        scr = lv_scr_act();
        
        lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(scr, lv_color_white(), LV_PART_MAIN);

        led_status = lv_led_create(scr);
        lv_obj_align(led_status, LV_ALIGN_TOP_LEFT, 30, 30);
        lv_obj_set_size(led_status, 15, 15);
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
        lv_led_on(led_status);

        label_status = lv_label_create(scr);
        lv_label_set_text(label_status, "Searching for BX-09");
        lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 55, 28);
        lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_text_font(label_status, &lv_font_montserrat_24, 0);

        lv_obj_t * lbl_cur_title = lv_label_create(scr);
        lv_label_set_text(lbl_cur_title, "CURRENT SP");
        lv_obj_align(lbl_cur_title, LV_ALIGN_TOP_LEFT, 30, 80);
        lv_obj_set_style_text_color(lbl_cur_title, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(lbl_cur_title, &lv_font_montserrat_24, 0);

        label_current_rpm = lv_label_create(scr);
        lv_label_set_text(label_current_rpm, "0");
        lv_obj_align(label_current_rpm, LV_ALIGN_TOP_LEFT, 30, 110);
        lv_obj_set_style_text_font(label_current_rpm, &lv_font_montserrat_48, 0); 

        lv_obj_t * lbl_best_title = lv_label_create(scr);
        lv_label_set_text(lbl_best_title, "ALL-TIME BEST");
        lv_obj_align(lbl_best_title, LV_ALIGN_TOP_LEFT, 30, 200);
        lv_obj_set_style_text_color(lbl_best_title, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_obj_set_style_text_font(lbl_best_title, &lv_font_montserrat_24, 0);

        label_all_time_rpm = lv_label_create(scr);
        prefs.begin("bx09_store", true);
        global_all_time_best = prefs.getUInt("best_rpm", 0); 
        global_hist_count = prefs.getInt("hist_cnt", 0);
        if (global_hist_count > 0) {
            prefs.getBytes("hist_arr", global_history, sizeof(global_history));
        }
        prefs.end(); 

        lv_label_set_text_fmt(label_all_time_rpm, "%d SP", global_all_time_best);
        lv_obj_align(label_all_time_rpm, LV_ALIGN_TOP_LEFT, 30, 230);
        lv_obj_set_style_text_font(label_all_time_rpm, &lv_font_montserrat_36, 0);

        lv_obj_t * lbl_hist_title = lv_label_create(scr);
        lv_label_set_text(lbl_hist_title, "Recent History");
        lv_obj_align(lbl_hist_title, LV_ALIGN_TOP_LEFT, 350, 30);
        lv_obj_set_style_text_color(lbl_hist_title, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_font(lbl_hist_title, &lv_font_montserrat_24, 0);

        label_history = lv_label_create(scr);
        String histText = "";
        for (int i = 0; i < global_hist_count; i++) {
            histText += String(i + 1) + ". " + String(global_history[i]) + " SP\n";
        }
        if (global_hist_count == 0) {
            histText = "1. -\n2. -\n3. -\n4. -\n5. -\n6. -\n7. -\n8. -";
        }

        lv_label_set_text(label_history, histText.c_str()); 
        lv_obj_align(label_history, LV_ALIGN_TOP_LEFT, 350, 65);
        lv_obj_set_style_text_line_space(label_history, 8, 0);
        lv_obj_set_style_text_font(label_history, &lv_font_montserrat_24, 0);

        chart = lv_chart_create(scr);
        lv_obj_set_size(chart, 250, 210); 
        lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 550, 70);
        
        lv_obj_set_style_bg_opa(chart, 0, LV_PART_MAIN);     
        lv_obj_set_style_border_color(chart, lv_color_hex(0x333333), LV_PART_MAIN); 
        
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        chart_series = lv_chart_add_series(chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
        lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
        
        label_battery = lv_label_create(scr);
        lv_obj_align(label_battery, LV_ALIGN_TOP_RIGHT, -20, 20); 
        lv_obj_set_style_text_font(label_battery, &lv_font_montserrat_24, 0);
        
        lv_label_set_text(label_battery, "--% (--V) " LV_SYMBOL_BATTERY_FULL); 
    } 
     // 🟢 繪製大紅電池低電量關機警告畫面
    void showLowBatteryScreen() {
        lv_obj_t * overlay = lv_obj_create(scr);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_border_width(overlay, 0, 0);

        // 大紅電池圖示
        lv_obj_t * icon = lv_label_create(overlay);
        lv_label_set_text(icon, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -35);

        // 警告警示文字
        lv_obj_t * msg = lv_label_create(overlay);
        lv_label_set_text(msg, "LOW BATTERY\nPLEASE CHARGE");
        lv_obj_set_style_text_color(msg, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, 35);

        // 強制刷新 LVGL 畫面渲染
        lv_timer_handler();
    }
    void updateStatus(int state) {
        if (state >= 0 && state <= 3) {
            requested_ble_state = state;
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
        if (readyToDraw) {
            readyToDraw = false; 
            
            // 🟢 修正 2：在主迴圈中安全地呼叫 LVGL 繪圖，完美避開當機與掉幀！
            if (BLE_Manager::liveCurveCount > 0) {
                updateChartCurve(BLE_Manager::liveCurveBuffer, BLE_Manager::liveCurveCount);
                BLE_Manager::liveCurveCount = 0; // 畫完後清空，等待下次發射
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