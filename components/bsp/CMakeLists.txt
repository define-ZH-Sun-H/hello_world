set(src_dirs
            iic
            oled)

set(include_dirs
            iic
            oled)

set(requires
            driver)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
