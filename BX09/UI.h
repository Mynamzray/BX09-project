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
    lv_obj_t * label_orig_sp; 
    lv_obj_t * label_all_time_rpm;
    lv_obj_t * label_history;
    lv_obj_t * chart;
    lv_obj_t * label_battery; 
    lv_chart_series_t * chart_series;

    // 🟢 記錄目前畫面上顯示的數字，作為下次動畫的起點
    static int current_displayed_rpm = 0;

    static void anim_text_update_cb(void * var, int32_t v) {
        lv_obj_t * label = (lv_obj_t *)var;
        lv_label_set_text_fmt(label, "%d RPM", v); 
    }

    void updateBattery(int percentage, bool isCharging) {
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
        lv_label_set_text_fmt(label_battery, "%d%% %s", percentage, symbol);
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
        // 呼叫滾動動畫，把官方分數顯示在最大的數字位置！
        updateCurrentRPM(origSP);
        // 🟢 1. 備份到 ESP32 的全域變數中
        global_hist_count = histCount;
        for (int i = 0; i < histCount; i++) {
            global_history[i] = history[i];
        }

        // 🟢 2. 寫入 Flash 永久保存，讓下次開機有資料可以顯示
        prefs.begin("bx09_store", false); 
        prefs.putInt("hist_cnt", global_hist_count); 
        prefs.putBytes("hist_arr", global_history, sizeof(global_history)); 
        prefs.end();

        if (label_history) {
            String histText = "";
            for (int i = 0; i < histCount; i++) {
                histText += String(i + 1) + ". " + String(history[i]) + " RPM\n";
            }
            if (histCount == 0) {
                histText = "No Official Data";
            }
            lv_label_set_text(label_history, histText.c_str());
        }
    }

    // 1. 初始化介面
    void init() {
        lvgl_port_init();
        lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

        scr = lv_scr_act();
        
        lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(scr, lv_color_white(), LV_PART_MAIN);

        // 📊 2. 左欄：實時數據區 (X 座標 30)
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

        // 實時轉速
        lv_obj_t * lbl_cur_title = lv_label_create(scr);
        lv_label_set_text(lbl_cur_title, "CURRENT RPM"); 
        lv_obj_align(lbl_cur_title, LV_ALIGN_TOP_LEFT, 30, 80);
        lv_obj_set_style_text_color(lbl_cur_title, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_set_style_text_font(lbl_cur_title, &lv_font_montserrat_24, 0);

        label_current_rpm = lv_label_create(scr);
        // 🟢 【修改】開機時，把歷史陣列也一起從硬碟讀出來！
        prefs.begin("bx09_store", true);
        global_all_time_best = prefs.getUInt("best_rpm", 0); 
        global_hist_count = prefs.getInt("hist_cnt", 0);
        if (global_hist_count > 0) {
            prefs.getBytes("hist_arr", global_history, sizeof(global_history));
        }
        prefs.end();

        lv_label_set_text(label_current_rpm, "0");
        lv_obj_align(label_current_rpm, LV_ALIGN_TOP_LEFT, 30, 110);
        lv_obj_set_style_text_font(label_current_rpm, &lv_font_montserrat_48, 0); 

        // 歷史最高
        lv_obj_t * lbl_best_title = lv_label_create(scr);
        lv_label_set_text(lbl_best_title, "ALL-TIME BEST");
        lv_obj_align(lbl_best_title, LV_ALIGN_TOP_LEFT, 30, 200);
        lv_obj_set_style_text_color(lbl_best_title, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_obj_set_style_text_font(lbl_best_title, &lv_font_montserrat_24, 0);

        label_all_time_rpm = lv_label_create(scr);
        prefs.begin("bx09_store", true);
        global_all_time_best = prefs.getUInt("best_rpm", 0); 
        prefs.end(); 

        lv_label_set_text_fmt(label_all_time_rpm, "%d RPM", global_all_time_best);
        lv_obj_align(label_all_time_rpm, LV_ALIGN_TOP_LEFT, 30, 230);
        lv_obj_set_style_text_font(label_all_time_rpm, &lv_font_montserrat_36, 0);

        // 📝 3. 中欄：歷史紀錄區 (X 座標 350)
        lv_obj_t * lbl_hist_title = lv_label_create(scr);
        lv_label_set_text(lbl_hist_title, "RECENT HISTORY"); 
        lv_obj_align(lbl_hist_title, LV_ALIGN_TOP_LEFT, 350, 30);
        lv_obj_set_style_text_color(lbl_hist_title, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_font(lbl_hist_title, &lv_font_montserrat_24, 0);

// 🟢 修復：移除佔版面的 Waiting 文字，讓畫面開機時保持絕對乾淨
        label_history = lv_label_create(scr);
        // 🟢 【修改】開機時，將剛剛讀取出來的離線歷史直接印在畫面上
        String histText = "";
        for (int i = 0; i < global_hist_count; i++) {
            histText += String(i + 1) + ". " + String(global_history[i]) + " RPM\n";
        }
        if (global_hist_count == 0) {
            histText = "Waiting for Sync..."; 
        }
        lv_label_set_text(label_history, histText.c_str());
        lv_obj_align(label_history, LV_ALIGN_TOP_LEFT, 350, 65);
        lv_obj_set_style_text_line_space(label_history, 8, 0);
        lv_obj_set_style_text_font(label_history, &lv_font_montserrat_24, 0);

        // 📈 4. 右欄：圖表 (X 座標 550)
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
        
        // 🔋 5. 電池狀態區 (右上角)
        label_battery = lv_label_create(scr);
        lv_obj_align(label_battery, LV_ALIGN_TOP_RIGHT, -20, 20); 
        lv_obj_set_style_text_font(label_battery, &lv_font_montserrat_24, 0);
        lv_label_set_text(label_battery, LV_SYMBOL_BATTERY_FULL " --%"); 
    }

    void updateStatus(int state) {
        if (state >= 0 && state <= 3) {
            requested_ble_state = state;
        }
    }

    void renderStatusUI(int state) { 
        if (state == 0) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
            lv_label_set_text(label_status, "BLE DISCONNECTED");
        } else if (state == 1) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_AMBER)); 
            lv_label_set_text(label_status, "WAITING FOR BEY");
        } else if (state == 2) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREEN)); 
            lv_label_set_text(label_status, "BEYBLADE READY");
        } else if (state == 3) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREY)); 
            lv_label_set_text(label_status, "SYSTEM PAUSED");
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

        prefs.begin("bx09_store", false); 
        if (needSaveBest) {
            prefs.putUInt("best_rpm", global_all_time_best);
        }
        prefs.end();

        lv_label_set_text_fmt(label_all_time_rpm, "%d RPM", global_all_time_best);
        
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
