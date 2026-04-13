idf_component_register(
    SRCS "EspNow.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event nvs_flash
)
