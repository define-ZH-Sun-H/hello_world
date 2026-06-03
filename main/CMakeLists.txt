idf_component_register(SRCS "queue_demo.c" "wifi.c" "main.c"
                       PRIV_REQUIRES spi_flash bsp spiffs nvs_flash esp_wifi esp_event esp_netif
                       INCLUDE_DIRS "")
