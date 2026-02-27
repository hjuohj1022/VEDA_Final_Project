FILE(GLOB_RECURSE app_sources *.c)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "mqtt" "certs" "device"
    EMBED_TXTFILES "certs/ca_cert.pem" 
                   "certs/client_cert.pem" 
                   "certs/client_key.pem"
)
