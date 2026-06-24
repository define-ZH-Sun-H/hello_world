idf_component_register(SRCS "wifi.c" "mqtt.c" "sntp.c" "network.c" "http_dl.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bsp nvs_flash esp_wifi esp_event esp_netif mqtt json ota esp_http_client)
