set(SOURCES
    "app_main.c"
    "mqtt_client/mqtt.c"
    "gps/gps.c"
    "4g/modem_4g.c"
    "rgb_led/led.c"
    "sensors/sensors.c"
    "data_manager/json_wrapper.c"
    "data_manager/data_model.c"
    "http_server/modem_http_config.c"
    "time/time_sync.c"
    "network_manager/network_manager.c"
    "network_manager/wifi_manager.c"
    "OTA/ota.c"
)

set(INCLUDES
    "."
    "mqtt_client"
    "4g"
    "gps"
    "rgb_led"
    "sensors"
    "data_manager"
    "http_server"
    "time"
    "network_manager"
    "OTA"
)

idf_component_register(SRCS ${SOURCES}
                INCLUDE_DIRS ${INCLUDES}
                EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)

target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-format)

include_directories(${INCLUDES})