FILE(GLOB_RECURSE app_sources *.c)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "mqtt" "certs" "device"
    REQUIRES esp_event esp_netif esp_system esp_timer esp_wifi mqtt nvs_flash lwip mbedtls
             esp_driver_gpio esp_driver_spi esp_driver_uart
    EMBED_TXTFILES "certs/rootCA.crt"
                   "certs/client-stm32.crt"
                   "certs/client-stm32.key"
)
