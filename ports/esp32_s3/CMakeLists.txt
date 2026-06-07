# MMBasic Anywhere - ESP32-S3 port
#
# Phase A: empty ESP-IDF project. Blink + USB Serial/JTAG hello.
# Phase B will add the MMBasic core sources to main/CMakeLists.txt.
#
# Build: from this directory,
#   . ~/esp/esp-idf/export.sh
#   idf.py set-target esp32s3
#   idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mmbasic_anywhere_esp32_s3)
