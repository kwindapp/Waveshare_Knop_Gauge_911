#include "lcd_bsp.h"
#include "cst816.h"
#include "lcd_bl_pwm_bsp.h"
#include "lcd_config.h"
#include "ui.h"

#include <esp_now.h>
#include <esp_wifi.h>   // for esp_now_recv_info_t on core 3.x
#include <WiFi.h>
#include "lvgl.h"

//====== CUSTOM FONTS ======
// Only keep the one we know exists (Hollow85).
extern const lv_font_t ui_font_Hollow85;
extern const lv_font_t ui_font_Hollow38;
extern const lv_font_t ui_font_Hollow22;
extern const lv_font_t ui_font_t20;
//==================================================
//              GLOBAL COLOR FLAGS
//==================================================

// SCREEN BG
#define SCR_BG_TOP      0x141414
#define SCR_BG_BOTTOM   0x857B7A

// TICKS
#define TICK_COLOR      0xD6CFCB

// NUMBERS
#define NUMBER_COLOR    0xB3B9C4

// RPM TEXT
#define RPM_TEXT_COLOR  0xDBD3D3

// NAME TEXT
#define NAME_COLOR      0x85807F
#define NAME_COLOR1     0x3B3333   // RAUH Welt
#define NAME_COLOR2     0x3B3333   // rwb janine

// ARC COLORS
#define ARC_GREEN       0x21543A
#define ARC_YELLOW      0x807116
#define ARC_RED         0xA64E12

// NEEDLE COLOR
#define NEEDLE_COLOR    0xFF4444

// CENTER HUB COLORS
#define HUB_BG          0x141414
#define HUB_BORDER      0x403B3B

//==================================================
//           LABEL OFFSETS
//==================================================
static const int RPM_Y_OFFSET    =  50;

static const int NAME0_Y_OFFSET  = -72;  // "911"
static const int NAME_Y_OFFSET   = -33;  // "porsche"
static const int NAME1_Y_OFFSET  = 100;  // "rwb janine"
static const int NAME2_Y_OFFSET  = 130;  // "RAUH Welt BEGRIFF"

//==================================================
//            ESP-NOW PACKET STRUCT
//==================================================
typedef struct {
  uint16_t rpm;
  float batt;
  float motor;
  float dk;
  float gp;
  uint8_t funk;
} DashPacket;

DashPacket lastPacket;
volatile uint16_t g_rpm = 0;
unsigned long lastPacketTime = 0;

//==================================================
//                 GAUGE OBJECTS
//==================================================
static lv_obj_t *g_meter        = nullptr;
static lv_meter_indicator_t *g_needle = nullptr;
static lv_obj_t *g_rpm_label    = nullptr;
static lv_obj_t *g_name0_label  = nullptr;
static lv_obj_t *g_name_label   = nullptr;
static lv_obj_t *g_name1_label  = nullptr;
static lv_obj_t *g_name2_label  = nullptr;
static lv_obj_t *g_center       = nullptr;

//==================================================
//                 CREATE GAUGE
//==================================================
void Lvgl_ShowGauge() {

  // Screen background
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(SCR_BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(SCR_BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  g_meter = lv_meter_create(scr);
  lv_obj_set_size(g_meter, 370, 370);
  lv_obj_center(g_meter);

  // Gauge background
  lv_obj_set_style_bg_color(g_meter, lv_color_hex(SCR_BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(g_meter, lv_color_hex(SCR_BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(g_meter, LV_GRAD_DIR_VER, 0);

  lv_meter_scale_t *scale = lv_meter_add_scale(g_meter);

  // Minor ticks
  lv_meter_set_scale_ticks(g_meter, scale, 9, 2, 14, lv_color_hex(TICK_COLOR));

  // Major ticks
  lv_meter_set_scale_major_ticks(
      g_meter, scale,
      1, 6, 24,
      lv_color_hex(TICK_COLOR),
      12
  );

  // Number styling – default font
  lv_obj_set_style_text_font(g_meter, LV_FONT_DEFAULT, LV_PART_TICKS);
  lv_obj_set_style_text_color(g_meter, lv_color_hex(NUMBER_COLOR), LV_PART_TICKS);

  // Scale 0–8
  lv_meter_set_scale_range(g_meter, scale, 0, 8, 270, 135);

  // ARC green
  lv_meter_indicator_t *ind_green = lv_meter_add_arc(g_meter, scale, 20, lv_color_hex(ARC_GREEN), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_green, 0);
  lv_meter_set_indicator_end_value(g_meter, ind_green, 4);

  // ARC yellow
  lv_meter_indicator_t *ind_yellow = lv_meter_add_arc(g_meter, scale, 24, lv_color_hex(ARC_YELLOW), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_yellow, 4);
  lv_meter_set_indicator_end_value(g_meter, ind_yellow, 6);

  // ARC red
  lv_meter_indicator_t *ind_red = lv_meter_add_arc(g_meter, scale, 30, lv_color_hex(ARC_RED), 0);
  lv_meter_set_indicator_start_value(g_meter, ind_red, 6);
  lv_meter_set_indicator_end_value(g_meter, ind_red, 8);

  // Needle
  g_needle = lv_meter_add_needle_line(g_meter, scale, 6, lv_color_hex(NEEDLE_COLOR), -45);

  // RPM TEXT (big Hollow85)
  g_rpm_label = lv_label_create(g_meter);
  lv_label_set_text(g_rpm_label, "0000");
  lv_obj_set_style_text_font(g_rpm_label, &ui_font_Hollow85, 0);
  lv_obj_set_style_text_color(g_rpm_label, lv_color_hex(RPM_TEXT_COLOR), 0);
  lv_obj_align(g_rpm_label, LV_ALIGN_CENTER, 0, RPM_Y_OFFSET);

  // "911" (big Hollow85)
  g_name0_label = lv_label_create(g_meter);
  lv_label_set_text(g_name0_label, "911");
  lv_obj_set_style_text_font(g_name0_label, &ui_font_Hollow85, 0);
  lv_obj_set_style_text_color(g_name0_label, lv_color_hex(NAME_COLOR), 0);
  lv_obj_align(g_name0_label, LV_ALIGN_CENTER, 0, NAME0_Y_OFFSET);

  // "porsche"
  g_name_label = lv_label_create(g_meter);
  lv_label_set_text(g_name_label, "porsche");
  lv_obj_set_style_text_font(g_name_label, &ui_font_Hollow38, 0);
  lv_obj_set_style_text_color(g_name_label, lv_color_hex(NAME_COLOR), 0);
  lv_obj_align(g_name_label, LV_ALIGN_CENTER, 0, NAME_Y_OFFSET);

  // "rwb janine"
  g_name1_label = lv_label_create(g_meter);
  lv_label_set_text(g_name1_label, "rwb janine");
  lv_obj_set_style_text_font(g_name1_label, &ui_font_Hollow38, 0);
  lv_obj_set_style_text_color(g_name1_label, lv_color_hex(NAME_COLOR2), 0);
  lv_obj_align(g_name1_label, LV_ALIGN_CENTER, 0, NAME1_Y_OFFSET);

  // "RAUH Welt BEGRIFF"
  g_name2_label = lv_label_create(g_meter);
  lv_label_set_text(g_name2_label, "RAUH Welt BEGRIFF");
  lv_obj_set_style_text_font(g_name2_label, &ui_font_t20, 0);
  lv_obj_set_style_text_color(g_name2_label, lv_color_hex(NAME_COLOR1), 0);
  lv_obj_align(g_name2_label, LV_ALIGN_CENTER, 0, NAME2_Y_OFFSET);

  // CENTER HUB
  g_center = lv_obj_create(g_meter);
  lv_obj_set_size(g_center, 40, 40);
  lv_obj_center(g_center);
  lv_obj_set_style_radius(g_center, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_center, lv_color_hex(HUB_BG), 0);
  lv_obj_set_style_border_color(g_center, lv_color_hex(HUB_BORDER), 0);
  lv_obj_set_style_border_width(g_center, 3, 0);
}

//==================================================
//           ESP-NOW RECEIVER CALLBACK
//==================================================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  Serial.println("\n===== ESP-NOW PACKET RECEIVED =====");

  if (recv_info) {
    Serial.printf("From: %02X:%02X:%02X:%02X:%02X:%02X\n",
        recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
        recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
  }
  Serial.printf("Len: %d\n", len);

  if (len != sizeof(DashPacket)) {
    Serial.printf("Invalid packet size (%d != %d)\n", len, sizeof(DashPacket));
    return;
  }

  memcpy(&lastPacket, incomingData, sizeof(DashPacket));

  Serial.printf("RPM  : %u\n", lastPacket.rpm);
  Serial.printf("Batt : %.2f\n", lastPacket.batt);
  Serial.printf("Motor: %.2f\n", lastPacket.motor);
  Serial.printf("DK   : %.1f\n", lastPacket.dk);
  Serial.printf("GP   : %.1f\n", lastPacket.gp);
  Serial.printf("Funk : %u\n", lastPacket.funk);

  if (lastPacket.rpm > 8000) lastPacket.rpm = 8000;

  g_rpm = lastPacket.rpm;
  lastPacketTime = millis();
}

//==================================================
//           ESP-NOW INITIALIZATION
//==================================================
void setupEspNow() {
  Serial.println("Starting ESP-NOW Receiver...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  esp_err_t err = esp_now_init();
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("esp_now_init FAILED: 0x%X\n", err);
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.print("WiFi STA MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("ESP-NOW READY (RX ONLY).");
}

//==================================================
//         LVGL TASK: UPDATE GAUGE FROM g_rpm
//==================================================
static void lvgl_task(void *arg)
{
  for(;;)
  {
    lv_timer_handler();

    static uint16_t last_rpm = 0;
    uint16_t rpm = g_rpm;

    if (rpm != last_rpm) {
      last_rpm = rpm;

      float v = rpm / 1000.0f;
      if (v > 8) v = 8;

      if (g_meter && g_needle) {
        lv_meter_set_indicator_value(g_meter, g_needle, v);
      }
      if (g_rpm_label) {
        lv_label_set_text_fmt(g_rpm_label, "%u", rpm);
      }

      Serial.printf("Gauge update: RPM=%u\n", rpm);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

//==================================================
//                     SETUP
//==================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.printf("DashPacket sizeof() on RX: %d bytes\n", sizeof(DashPacket));

  Touch_Init();
  lcd_lvgl_Init();          // board LVGL init
  Lvgl_ShowGauge();         // create our Porsche gauge
  lcd_bl_pwm_bsp_init(40);  // brightness (0–255)

  setupEspNow();

  // Start LVGL task
  xTaskCreate(lvgl_task, "lvgl",
              EXAMPLE_LVGL_TASK_STACK_SIZE,
              NULL,
              EXAMPLE_LVGL_TASK_PRIORITY,
              NULL);

  Serial.println("Setup complete. Waiting for ESP-NOW data...");
}

//==================================================
//                     LOOP
//==================================================
void loop() {
  // nothing – everything runs in tasks
}
