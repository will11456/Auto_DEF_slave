SET(SOURCES
    screens/ui_DataScreen.c
    screens/ui_SplashScreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_dfs_logo_png_png.c
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES lvgl
)