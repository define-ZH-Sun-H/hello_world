idf_component_register(SRCS "queue_demo.c" "wifi.c" "main.c" "menu.c" "sys_ctrl.c" "mqtt.c" "sys_info.c" "storage_init.c"
                       PRIV_REQUIRES spi_flash bsp spiffs nvs_flash esp_wifi esp_event esp_netif mqtt app
                       INCLUDE_DIRS "")
