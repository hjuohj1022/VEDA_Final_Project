FILE(GLOB_RECURSE app_sources *.c)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "mqtt" "certs" "device"
    EMBED_TXTFILES "certs/rootCA.crt"
                   "certs/client-stm32.crt"
                   "certs/client-stm32.key"
)
