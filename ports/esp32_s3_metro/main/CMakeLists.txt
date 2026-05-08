# Phase A: just app_main.c. Phase B will pull in the MMBasic core
# sources via SRCS "../../../MMBasic.c" etc., and add the matching
# INCLUDE_DIRS.

idf_component_register(
    SRCS
        "app_main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_timer
)
