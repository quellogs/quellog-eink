idf_component_register(
    SRCS
        "main.cc"
    REQUIRES
        esp_pm
        nvs_flash
        quellog_app
)
