set(src_dirs
            .
            iic
            key
            led
            rgb
            sd
            sensor
            audio
            debug
            touch_lcd)

set(include_dirs
            .
            iic
            key
            led
            rgb
            sd
            sensor
            audio
            debug
            touch_lcd
            camera)

set(requires
            driver
            fatfs
            app
            lvgl
            esp_lcd)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
