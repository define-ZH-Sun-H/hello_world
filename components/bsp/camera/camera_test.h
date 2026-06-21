#ifndef _CAMERA_TEST_H
#define _CAMERA_TEST_H

#include "esp_err.h"

/**
 * @brief 初始化 OV2640 摄像头
 *
 * @return ESP_OK 成功，否则错误码
 */
esp_err_t camera_test_init(void);

/**
 * @brief 拍摄一张照片并可选保存到文件
 *
 * @param save_path 保存路径（传 NULL 则不保存）
 *
 * @return ESP_OK 成功，否则错误码
 */
esp_err_t camera_test_capture(const char *save_path);

#endif
