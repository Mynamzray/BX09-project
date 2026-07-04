#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "lcd_bl_pwm_bsp.h"

// 🟢 關鍵：預先宣告藍牙開關，讓 UI 模組知道有這個功能存在
namespace BLE_Manager {
    void toggleBluetooth();
}

// 將原本宣告在全域的記憶庫與變數搬過來

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

// 将原本宣告在全域的記憶庫與變數搬過來
Preferences prefs;
uint16_t global_all_time_best = 0; 
uint16_t global_history[8] = {0};
int global_hist_count = 0;


// ==========================================
// [模組 2] LVGL 渲染器 (橫向超寬儀表板版)
// ==========================================
//extern const lv_font_t lv_font_montserrat_24;
//extern const lv_font_t lv_font_montserrat_36;
//extern const lv_font_t lv_font_montserrat_48;
namespace UI {
    volatile bool readyToDraw = false;

    // LVGL 物件指標
    lv_obj_t * scr;
    lv_obj_t * label_status;
    lv_obj_t * led_status;
    lv_obj_t * label_current_rpm;
    lv_obj_t * label_all_time_rpm;
    lv_obj_t * label_history;
    lv_obj_t * chart;
    lv_obj_t * label_battery; // 🟢 新增：電池文字與圖示物件
    lv_chart_series_t * chart_series;

// 🟢 記錄目前畫面上顯示的數字，作為下次動畫的起點
    static int current_displayed_rpm = 0;

    // 🟢 動畫引擎專用的回呼函式：LVGL 每畫一格都會呼叫這裡，傳入計算好的漸變數字 (v)
    static void anim_text_update_cb(void * var, int32_t v) {
        lv_obj_t * label = (lv_obj_t *)var;
        // 把漸變中的數字格式化並顯示出來
        lv_label_set_text_fmt(label, "%d RPM", v); 
    }

// 🟢 新增：更新電池圖示與百分比
 // 🟢 修正版：更新電池圖示與百分比 (支援充電狀態)
    void updateBattery(int percentage, bool isCharging) {
        if (!label_battery) return;

        const char* symbol;
        
        if (isCharging) {
            symbol = LV_SYMBOL_CHARGE; // ⚡ 顯示 LVGL 內建閃電符號
            lv_obj_set_style_text_color(label_battery, lv_palette_main(LV_PALETTE_GREEN), 0); // 充電時變綠色
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
            
            // 低電量警告
            if (percentage <= 20) {
                lv_obj_set_style_text_color(label_battery, lv_palette_main(LV_PALETTE_RED), 0);
            } else {
                lv_obj_set_style_text_color(label_battery, lv_color_white(), 0);
            }
        }
    }
// 🟢 假設這是你用來更新轉速的函式，請把 label_current_rpm 換成你實際的標籤變數名稱
    void updateCurrentRPM(int target_rpm) {
        if (!label_current_rpm) return;

        // 如果轉速沒變，就不需要播動畫
        if (current_displayed_rpm == target_rpm) return;

        // 建立並設定 LVGL 動畫物件
        lv_anim_t a;
        lv_anim_init(&a);
        
        // 1. 指定要操作的物件 (你的數字標籤)
        lv_anim_set_var(&a, label_current_rpm);
        
        // 2. 設定起點與終點 (從目前的數字，滾動到新的目標轉速)
        // 如果想要每次都從 0 開始滾，可以把 current_displayed_rpm 改成 0
        lv_anim_set_values(&a, current_displayed_rpm, target_rpm);
        
        // 3. 設定動畫持續時間：500 毫秒 (0.5秒)
        lv_anim_set_time(&a, 500);
        
        // 4. 設定動畫曲線：Ease-out (先快後慢，非常有跑車儀表板的感覺)
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        
        // 5. 指定執行動畫的回呼函式
        lv_anim_set_exec_cb(&a, anim_text_update_cb);
        
        // 6. 啟動動畫！
        lv_anim_start(&a);

        // 更新歷史紀錄，讓下一次動畫知道要從哪裡開始
        current_displayed_rpm = target_rpm;
    }    

    // 1. 初始化介面
void init() {
        lvgl_port_init();
        lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

        lv_obj_t * scr = lv_scr_act();
        
        // 🎨 1. 絕對純黑主題
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
        lv_label_set_text(label_current_rpm, "0");
        lv_obj_align(label_current_rpm, LV_ALIGN_TOP_LEFT, 30, 110);
        // 🟢 直接套用穩定的 24 號字體，拔除所有 zoom 相關設定！
        lv_obj_set_style_text_font(label_current_rpm, &lv_font_montserrat_48, 0); 

        // 歷史最高
        lv_obj_t * lbl_best_title = lv_label_create(scr);
        lv_label_set_text(lbl_best_title, "ALL-TIME BEST");
        lv_obj_align(lbl_best_title, LV_ALIGN_TOP_LEFT, 30, 200);
        lv_obj_set_style_text_color(lbl_best_title, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_obj_set_style_text_font(lbl_best_title, &lv_font_montserrat_24, 0);

        label_all_time_rpm = lv_label_create(scr);
        // 🟢 【新增】開機時，打開名為 "bx09_store" 的空間（true 代表唯讀模式）
        prefs.begin("bx09_store", true);
        // 讀取名為 "best_rpm" 的數字，如果裡面從來沒存過東西，預設值給 0
        global_all_time_best = prefs.getUInt("best_rpm", 0); 
        global_hist_count = prefs.getInt("hist_cnt", 0);
        if (global_hist_count > 0) {
            prefs.getBytes("hist_arr", global_history, sizeof(global_history));
        }
        prefs.end(); // 關閉空間
        lv_label_set_text_fmt(label_all_time_rpm, "%d RPM", global_all_time_best);
        lv_obj_align(label_all_time_rpm, LV_ALIGN_TOP_LEFT, 30, 230);
        // 🟢 拔除 zoom，套用 24 號字體
        lv_obj_set_style_text_font(label_all_time_rpm, &lv_font_montserrat_36, 0);

        // 📝 3. 中欄：歷史紀錄區 (X 座標 350)
        lv_obj_t * lbl_hist_title = lv_label_create(scr);
        lv_label_set_text(lbl_hist_title, "Recent History");
        lv_obj_align(lbl_hist_title, LV_ALIGN_TOP_LEFT, 350, 30);
        lv_obj_set_style_text_color(lbl_hist_title, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_font(lbl_hist_title, &lv_font_montserrat_24, 0);

        label_history = lv_label_create(scr);
        String histText = "";
        for (int i = 0; i < global_hist_count; i++) {
            histText += String(i + 1) + ". " + String(global_history[i]) + " RPM\n";
        }
        // 如果完全沒紀錄，給予預設介面
        if (global_hist_count == 0) {
            histText = "1. -\n2. -\n3. -\n4. -\n5. -\n6. -\n7. -\n8. -";
        }

        // 🟢 正確修改：把組裝好的 histText 變數印出來！
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
        lv_obj_align(label_battery, LV_ALIGN_TOP_RIGHT, -20, 20); // 靠右上角，留一點邊界
        lv_obj_set_style_text_font(label_battery, &lv_font_montserrat_24, 0);
        lv_label_set_text(label_battery, LV_SYMBOL_BATTERY_FULL " --%"); // 開機預設顯示
    }
// 2. 更新藍牙狀態燈 (三段變速版)
    void updateStatus(int state) {
        if (state == 0) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
            lv_label_set_text(label_status, "BLE DISCONNECTED");
        } else if (state == 1) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_AMBER)); // 🟡 黃燈
            lv_label_set_text(label_status, "WAITING FOR BEY");
        } else if (state == 2) {
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREEN)); // 🟢 綠燈
            lv_label_set_text(label_status, "BEYBLADE READY");
            } else if (state == 3) {
            // 🟢 新增的休眠狀態
            lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREY)); 
            lv_label_set_text(label_status, "SYSTEM PAUSED");
        }
    }
// 3. 繪製結果
    void showResults(uint16_t currentPeak, uint16_t allTimePeak, float history[], int histCount, uint16_t turnData[], int turnCount) {
        
        bool needSaveBest = false;
        
        // 1. 檢查並更新歷史最高紀錄
        if (allTimePeak > global_all_time_best) {
            global_all_time_best = allTimePeak;
            needSaveBest = true;
        }

        // 2. 🟢 處理最新歷史紀錄：將陣列資料全部往後推一格，騰出第 0 格
        for (int i = 7; i > 0; i--) {
            global_history[i] = global_history[i - 1];
        }
        // 把最新的一次發射塞到最前面
        global_history[0] = currentPeak; 
        
        if (global_hist_count < 8) {
            global_hist_count++;
        }

        // 3. 🟢 將新資料寫入 ESP32 的 Flash 硬碟中
        prefs.begin("bx09_store", false); // 讀寫模式
        if (needSaveBest) {
            prefs.putUInt("best_rpm", global_all_time_best);
        }
        prefs.putInt("hist_cnt", global_hist_count); // 存數量
        prefs.putBytes("hist_arr", global_history, sizeof(global_history)); // 存陣列
        prefs.end();

        // 4. 更新左側 UI
        //lv_label_set_text_fmt(label_current_rpm, "%d RPM", currentPeak);
        //lv_label_set_text_fmt(label_all_time_rpm, "%d RPM", global_all_time_best);
        // 4. 更新左側 UI
        updateCurrentRPM(currentPeak); // 🟢 替換成這行！呼叫你的滾動動畫引擎！
        lv_label_set_text_fmt(label_all_time_rpm, "%d RPM", global_all_time_best);
        // 5. 更新中間歷史紀錄 UI
        String histText = "";
        for (int i = 0; i < global_hist_count; i++) {
            histText += String(i + 1) + ". " + String(global_history[i]) + " RPM\n";
        }
        lv_label_set_text(label_history, histText.c_str());

        // 6. 更新右側圖表 (維持原樣)
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

    // 4. 安全檢查
    void handleUpdate() {
        if (readyToDraw) {
            readyToDraw = false; 
            showResults(Physics::peak_rpm, Physics::allTimePeak, Physics::history, Physics::historyCount, Physics::SP, Physics::size);
        }
    }
}
