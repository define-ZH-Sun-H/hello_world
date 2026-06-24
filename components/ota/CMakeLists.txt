idf_component_register(SRCS "ota.c" "ota_gitee.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_http_client app_update json mbedtls esp_wifi)
