set(bridge_sources)
if(CONFIG_ESPNOW_RTCM_BASE OR CONFIG_ESPNOW_RTCM_ROVER)
    if(CONFIG_ESPNOW_RTCM_PEER_MAC STREQUAL "00:00:00:00:00:00")
        message(FATAL_ERROR "Configure the other board's Wi-Fi STA MAC before building ESP-NOW")
    endif()
    list(APPEND bridge_sources "src/espnow_rtcm_codec.c" "src/espnow_rtcm.c")
endif()
idf_component_register(
    SRCS ${bridge_sources}
    INCLUDE_DIRS "include"
    REQUIRES ug_rtcm rtcm_filter lora_transport esp_wifi esp_netif esp_event nvs_flash
             esp_timer esp_driver_usb_serial_jtag
)
