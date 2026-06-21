idf_component_register(SRCS "ota.c" "ota_github.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_http_client app_update json)
