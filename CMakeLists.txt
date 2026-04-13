idf_component_register(
    SRCS "src/EspNow.cpp" "src/EspNowNode.cpp"
    INCLUDE_DIRS "src"
    REQUIRES esp_wifi esp_event nvs_flash
)
