#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/i2c.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

#include "drivers/ssd1306.h"
#include "drivers/dht11.h"

#define TAG "HEALTH_MONITOR"

// ─── Konfigurasi WiFi ─────────────────────────────────────────
#define WIFI_SSID       "manghaji EXT"
#define WIFI_PASSWORD   "mutiahilman"

// ─── Konfigurasi Supabase ─────────────────────────────────────
#define SUPABASE_URL    "https://kihdzaaydgvjeyuzzgkv.supabase.co"
#define SUPABASE_KEY    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImtpaGR6YWF5ZGd2amV5dXp6Z2t2Iiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc4MTM1MjQwNCwiZXhwIjoyMDk2OTI4NDA0fQ.VZfgnw04VBpdE4Ic-s8TR7z31QiJvWgzB8mWXMeJlz4"
#define TARGET_USER_ID  "d8abdeb4-6340-4079-8c5e-ef98b0529b79"
#define TARGET_PROFILE_ID 1

// ─── Pin Hardware ─────────────────────────────────────────────
#define I2C_PORT    I2C_NUM_0
#define SDA_PIN     GPIO_NUM_21
#define SCL_PIN     GPIO_NUM_22
#define OLED_ADDR   0x3C
#define DHT_PIN     GPIO_NUM_4
#define BUZZER_PIN  GPIO_NUM_26

// ─── 15 Data Dummy ────────────────────────────────────────────
static const float dummy_bpm[]  = {75,78,72,80,76,79,71,82,74,77,73,81,75,78,72};
static const int   dummy_spo2[] = {98,97,99,96,98,97,99,95,98,97,99,96,98,97,99};
static const float dummy_temp[] = {36.5,36.8,36.3,37.0,36.6,36.9,36.2,37.1,36.4,36.7,36.3,37.0,36.5,36.8,36.3};
// Tanggal dummy untuk setiap index (dipakai saat upload)
static const char* dummy_date[] = {
    "2026-01-01","2026-01-02","2026-01-03","2026-01-04","2026-01-05",
    "2026-01-06","2026-01-07","2026-01-08","2026-01-09","2026-01-10",
    "2026-01-11","2026-01-12","2026-01-13","2026-01-14","2026-01-15"
};
#define DUMMY_COUNT 15

// ─── WiFi Event Group ─────────────────────────────────────────
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Menunggu koneksi WiFi...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi terhubung!");
}

// ─── Kirim Data ke Supabase ───────────────────────────────────
static void send_to_supabase(int idx, float temp, int spo2, float bpm)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT))
    {
        ESP_LOGW(TAG, "WiFi tidak tersedia, skip upload");
        return;
    }

    char json_body[512];
    char auth_header[256];

    snprintf(json_body, sizeof(json_body),
             "{\"user_id\":\"%s\","
             "\"profile_id\":%d,"
             "\"record_date\":\"%s\","
             "\"spo2\":%d,"
             "\"heart_rate\":%d,"
             "\"temp\":%.1f}",
             TARGET_USER_ID,
             TARGET_PROFILE_ID,
             dummy_date[idx],
             spo2,
             (int)bpm,
             temp);

    snprintf(auth_header, sizeof(auth_header), "Bearer %s", SUPABASE_KEY);

    esp_http_client_config_t config = {
        .url = SUPABASE_URL "/rest/v1/user_daily_monitoring",
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", SUPABASE_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Prefer", "resolution=merge-duplicates");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Supabase response: HTTP %d", esp_http_client_get_status_code(client));
    else
        ESP_LOGE(TAG, "HTTP gagal: %s", esp_err_to_name(err));

    esp_http_client_cleanup(client);
}

// ─── Inisialisasi Hardware ────────────────────────────────────
static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static void buzzer_init(void)
{
    gpio_reset_pin(BUZZER_PIN);
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(BUZZER_PIN, 0);
}

// ─── Main ─────────────────────────────────────────────────────
void app_main(void)
{
    // Nonaktifkan brownout detector
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    ESP_LOGI(TAG, "System Starting...");

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init hardware
    i2c_master_init();
    buzzer_init();

    if (!dht11_init(DHT_PIN))
        ESP_LOGE(TAG, "DHT11 Init Failed");

    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!ssd1306_init(I2C_PORT, OLED_ADDR))
    {
        ESP_LOGE(TAG, "OLED Init Failed");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ssd1306_clear();
    ssd1306_printf(0, 0, "CONNECTING...");
    ssd1306_display();

    // Hubungkan WiFi
    wifi_init();

    ssd1306_clear();
    ssd1306_printf(0, 0, "SYSTEM READY");
    ssd1306_printf(0, 2, "WiFi: OK");
    ssd1306_display();
    vTaskDelay(pdMS_TO_TICKS(2000));

    int idx = 0;

    while (1)
    {
        // Baca DHT11 (hanya humidity)
        dht11_data_t dht = {0};
        if (!dht11_read(&dht))
            ESP_LOGW(TAG, "DHT11 Read Failed");

        // Data dummy
        float bpm         = dummy_bpm[idx];
        int   spo2        = dummy_spo2[idx];
        float display_temp = dummy_temp[idx];
        float display_hum  = (dht.humidity > 0.0f) ? dht.humidity : 60.0f;

        // Buzzer alarm
        if (bpm > 120 || bpm < 50)
            gpio_set_level(BUZZER_PIN, 1);
        else
            gpio_set_level(BUZZER_PIN, 0);

        ESP_LOGI(TAG, "Temp=%.1fC Hum=%.1f%% BPM=%.0f SpO2=%d%%",
                 display_temp, display_hum, bpm, spo2);

        // Tampilkan di OLED
        ssd1306_clear();
        ssd1306_printf(0, 0, "HEALTH MONITOR");
        ssd1306_printf(0, 2, "Temp: %.1f C", display_temp);
        ssd1306_printf(0, 3, "Hum : %.1f %%", display_hum);
        ssd1306_printf(0, 5, "BPM : %.0f", bpm);
        ssd1306_printf(0, 6, "SpO2: %d %%", spo2);

        // Status upload
        ssd1306_printf(0, 7, "Uploading...");
        ssd1306_display();

        // Kirim ke Supabase
        send_to_supabase(idx, display_temp, spo2, bpm);

        ssd1306_printf(0, 7, "Upload: OK  ");
        ssd1306_display();

        // Maju ke data berikutnya
        idx++;
        if (idx >= DUMMY_COUNT) idx = 0;

        // Ganti data setiap 3 detik
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}