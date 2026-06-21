idf_component_register(SRCS "storage_init.c" "sys_info.c" "system.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bsp spiffs nvs_flash)
