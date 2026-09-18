idf_component_register(
    SRCS "main.c"
         "rid_store.c"
         "rid_odid.c"
         "rid_ui.c"
         "rid_radio.c"
         "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/rid_font_16.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/.."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
)
