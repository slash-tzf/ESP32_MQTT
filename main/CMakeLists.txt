set(SOURCES
    "app_main.c"
    "mqtt_client/mqtt.c"
    "gps/gps.c"
    "4g/modem_4g.c"
    "RGB_LED/led.c"
    "sensors/sensors.c"
    "data_manager/json_wrapper.c"
    "data_manager/data_model.c"
    "http_server/modem_http_config.c"
    "time/time_sync.c"
)

set(INCLUDES
    "."
    "mqtt_client"
    "4g"
    "gps"
    "RGB_LED"
    "sensors"
    "data_manager"
    "http_server"
    "time"
)

idf_component_register(SRCS ${SOURCES}
                INCLUDE_DIRS ${INCLUDES})

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)

include_directories(${INCLUDES})