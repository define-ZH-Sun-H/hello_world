idf_component_register(SRCS "app.c" "sys_ctrl.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bsp nvs_flash network)
