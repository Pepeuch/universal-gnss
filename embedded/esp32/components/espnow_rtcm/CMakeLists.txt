set(bridge_sources)
if(CONFIG_ESPNOW_RTCM_BASE OR CONFIG_ESPNOW_RTCM_ROVER)
    list(APPEND bridge_sources "src/espnow_rtcm_codec.c" "src/espnow_rtcm.c" "src/espnow_rtcm_output.c")
endif()
if(CONFIG_IDF_TARGET_ESP32S3 AND CONFIG_ESPNOW_RTCM_BASE)
    list(APPEND bridge_sources "src/espnow_rtcm_storage.c")
endif()
idf_component_register(
    SRCS ${bridge_sources}
    INCLUDE_DIRS "include"
    REQUIRES ug_pairing ug_rtcm rtcm_filter lora_transport esp_wifi esp_netif esp_event nvs_flash
             esp_timer esp_driver_usb_serial_jtag
)
if(CONFIG_IDF_TARGET_ESP32S3 AND CONFIG_ESPNOW_RTCM_BASE)
    idf_component_optional_requires(PRIVATE ug_supervisor)
endif()
