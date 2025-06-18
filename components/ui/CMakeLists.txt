SET(SOURCES screens/ui_DataScreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    )

    idf_component_register(SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES lvgl)
