idf_component_register(SRCS "src/ug_ethernet.c" INCLUDE_DIRS "include"
    REQUIRES esp_eth esp_netif esp_event esp_hw_support esp_driver_gpio esp_driver_spi
             espressif__w5500)
