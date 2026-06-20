#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_camera.h"

static const char *TAG = "camera";

/* 普中 ESP32-S3 开发板 OV2640 引脚映射 */
#define CAM_PIN_PWDN     -1
#define CAM_PIN_RESET    -1
#define CAM_PIN_XCLK     15
#define CAM_PIN_SIOD     4
#define CAM_PIN_SIOC     5

#define CAM_PIN_D7       11  /* Y2 */
#define CAM_PIN_D6       9   /* Y3 */
#define CAM_PIN_D5       8   /* Y4 */
#define CAM_PIN_D4       10  /* Y5 */
#define CAM_PIN_D3       12  /* Y6 */
#define CAM_PIN_D2       18  /* Y7 */
#define CAM_PIN_D1       17  /* Y8 */
#define CAM_PIN_D0       16  /* Y9 */

#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK     13

static bool s_camera_inited = false;

esp_err_t camera_test_init(void)
{
    if (s_camera_inited) {
        return ESP_OK;
    }

    camera_config_t config = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7       = CAM_PIN_D7,   /* Y2 */
        .pin_d6       = CAM_PIN_D6,   /* Y3 */
        .pin_d5       = CAM_PIN_D5,   /* Y4 */
        .pin_d4       = CAM_PIN_D4,   /* Y5 */
        .pin_d3       = CAM_PIN_D3,   /* Y6 */
        .pin_d2       = CAM_PIN_D2,   /* Y7 */
        .pin_d1       = CAM_PIN_D1,   /* Y8 */
        .pin_d0       = CAM_PIN_D0,   /* Y9 */

        .pin_vsync    = CAM_PIN_VSYNC,
        .pin_href     = CAM_PIN_HREF,
        .pin_pclk     = CAM_PIN_PCLK,

        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_QVGA,      /* 320x240，先跑通再提高 */
        .jpeg_quality = 12,
        .fb_count     = 1,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
        .fb_location  = CAMERA_FB_IN_PSRAM,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    s_camera_inited = true;

    /* 打印传感器信息 */
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "摄像头型号: 0x%x, 版本: 0x%x", s->id.PID, s->id.VER);
    }

    ESP_LOGI(TAG, "摄像头初始化完成 (QVGA 320x240 JPEG)");
    return ESP_OK;
}

esp_err_t camera_test_capture(const char *save_path)
{
    if (!s_camera_inited) {
        ESP_LOGE(TAG, "摄像头未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    /* 获取一帧 */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "获取帧失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "拍照成功: %ux%u, %u bytes, 格式=%u",
             fb->width, fb->height, fb->len, fb->format);

    /* 保存到文件（如果提供了路径） */
    if (save_path && save_path[0]) {
        FILE *f = fopen(save_path, "wb");
        if (f) {
            size_t written = fwrite(fb->buf, 1, fb->len, f);
            fclose(f);
            ESP_LOGI(TAG, "已保存: %s (%u bytes)", save_path, written);
        } else {
            ESP_LOGW(TAG, "无法写入: %s", save_path);
        }
    }

    esp_camera_fb_return(fb);
    return ESP_OK;
}
