#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "lcd_bl_pwm_bsp.h"
#include "Physics.h"

namespace BLE_Manager {
    void toggleBluetooth();
}

// ==========================================
// 🛠️ 跨筆電字體強行解鎖修復 (繞過 lv_conf.h)
// ==========================================
#ifdef __cplusplus
extern "C" {
#endif
    // 宣告 LVGL 字體結構
    #ifndef LV_FONT_DECLARE
        #define LV_FONT_DECLARE(font_name) extern const lv_font_t font_name;
    #endif

    // 強制把 24, 36, 48 號字體的點陣資料射進編譯器
    LV_FONT_DECLARE(lv_font_montserrat_24)
    LV_FONT_DECLARE(lv_font_montserrat_36)
    LV_FONT_DECLARE(lv_font_montserrat_48)
#ifdef __cplusplus
}
#endif

// 將原本宣告在全域的記憶庫與變數搬過來
Preferences prefs;
uint16_t global_all_time_best = 0; 
uint16_t global_history[8] = {0};
int global_hist_count = 0;

// ==========================================
// [模組 2] LVGL 渲染器 (橫向超寬儀表板版)
// ==========================================
namespace UI {
    volatile bool readyToDraw = false;

    // 🟢 核心：乾淨的信箱與狀態記憶
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
            if (percentage >= 80) {
                symbol = LV_SYMBOL_BATTERY_FULL;
            } else if (percentage >= 60) {
                symbol = LV_SYMBOL_BATTERY_3;
            } else if (percentage >= 40) {
                symbol = LV_SYMBOL_BATTERY_2;
            } else if (percentage >= 20) {
                symbol = LV_SYMBOL_BATTERY_1;
            } else {
                symbol = LV_SYMBOL_BATTERY_EMPTY;
            }
            
            if (percentage <= 20) {
                lv_obj_set_style_text_color(label_battery, lv_palette_main(LV_PALETTE_RED), 0);
            } else {
                lv_obj_set_style_text_color(label_battery, lv_color_white(), 0);
            }
        }
        
        // 🟢 救命修正：先用 Arduino String 將小數點轉成文字，再以字串 (%s) 餵給 LVGL
        // 這樣就能完美避開 LVGL 不支援浮點數 (%f) 的致命崩潰！
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
        lv_label_set_text(label_status, "BLE DISCONNECTED");
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

    void showResults(uint16_t currentPeak, uint16_t allTimePeak, float history[], int histCount, uint16_t turnData[], int turnCount) {
        bool needSaveBest = false;
        
        if (allTimePeak > global_all_time_best) {
            global_all_time_best = allTimePeak;
            needSaveBest = true;
        }

        for (int i = 7; i > 0; i--) {
            global_history[i] = global_history[i - 1];
        }
        global_history[0] = currentPeak; 
        
        if (global_hist_count < 8) {
            global_hist_count++;
        }

        prefs.begin("bx09_store", false); 
        if (needSaveBest) {
            prefs.putUInt("best_rpm", global_all_time_best);
        }
        prefs.putInt("hist_cnt", global_hist_count); 
        prefs.putBytes("hist_arr", global_history, sizeof(global_history)); 
        prefs.end();

        updateCurrentRPM(currentPeak); 
        lv_label_set_text_fmt(label_all_time_rpm, "%d SP", global_all_time_best);
        
        String histText = "";
        for (int i = 0; i < global_hist_count; i++) {
            histText += String(i + 1) + ". " + String(global_history[i]) + " SP\n";
        }
        lv_label_set_text(label_history, histText.c_str());

        if (turnCount > 0) {
            uint16_t maxRpmInChart = 0;
            for (int i = 0; i < turnCount; i++) {
                if (turnData[i] > maxRpmInChart) maxRpmInChart = turnData[i];
            }
            maxRpmInChart = maxRpmInChart * 1.15;
            if (maxRpmInChart < 1000) maxRpmInChart = 1000;

            lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, maxRpmInChart);
            lv_chart_set_point_count(chart, turnCount);
            
            for (int i = 0; i < turnCount; i++) {
                lv_chart_set_value_by_id(chart, chart_series, i, turnData[i]);
            }
            lv_chart_refresh(chart);
        }
    }

    void handleUpdate() {
        if (readyToDraw) {
            readyToDraw = false; 
            showResults(Physics::peak_rpm, Physics::allTimePeak, Physics::history, Physics::historyCount, Physics::SP, Physics::size);
            
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
