set(src_dirs
            iic
            key
            led
            oled
            rgb
            sensor)

set(include_dirs
            .
            iic
            key
            led
            oled
            rgb
            sensor)

set(requires
            driver)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
