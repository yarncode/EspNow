idf_component_register(
    SRCS "src/EspNow.cpp"
    INCLUDE_DIRS "src"
    REQUIRES esp_wifi esp_event nvs_flash
)
