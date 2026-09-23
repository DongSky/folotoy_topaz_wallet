idf_component_register(
    SRCS "main.c" "wallet_core.c" "passport_ui.c" "passport_nav.c" "passport_jpeg.c"
         "wallet_transfer.c" "wallet_store.c" "wallet_ble.c"
         "stock_ble.c" "stock_protocol.c" "stock_transfer.c" "stock_screenshot.c"
         "stock_content.c" "stock_identity.c" "stock_profile.c"
         "stock_avatar.c" "passport_capture.c" "stock_screenshot_session.c"
         "stock_music.c" "stock_rtttl.c"
         "stock_profile_json.c" "stock_profile_nvs.c"
         "../assets/fonts/card_font_18.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer nvs_flash esp_partition esp_hw_support esp_rom json
)
