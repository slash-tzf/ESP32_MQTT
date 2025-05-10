set(SOURCES
    "app_main.c"
    "mqtt_client/mqtt.c"
    "gps/gps.c"
    "4g/modem_4g.c"
    "RGB_LED/led.c"
)

set(INCLUDES
    "."
    "mqtt_client"
    "4g"
    "gps"
    "RGB_LED"
)

idf_component_register(SRCS ${SOURCES}
                INCLUDE_DIRS ${INCLUDES})

include_directories(${INCLUDES})