set(src_dirs
            .
            iic
            key
            led
            oled
            rgb
            sd
            sensor
            audio
            debug)

set(include_dirs
            .
            iic
            key
            led
            oled
            rgb
            sd
            sensor
            audio
            debug)

set(requires
            driver
            fatfs
            app)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
