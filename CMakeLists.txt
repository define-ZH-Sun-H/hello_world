# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# VSCode 扩展有时不传 IDF_PATH 环境变量，这里设一个 fallback
if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
    set(ENV{IDF_PATH} "/home/devcontainers/esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_world)

# SPIFFS 镜像：将 spiffs_image/ 目录打包到 storage 分区
spiffs_create_partition_image(storage spiffs_image)
