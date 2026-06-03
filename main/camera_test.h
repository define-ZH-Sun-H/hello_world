#ifndef _CAMERA_TEST_H
#define _CAMERA_TEST_H

#include "esp_err.h"

esp_err_t camera_test_init(void);
esp_err_t camera_test_capture(const char *save_path);

#endif
