#ifndef LED_H
#define LED_H

#include "led_strip.h"

extern led_strip_handle_t led_strip;

void _led_indicator_init();

void led_set_color(uint8_t r, uint8_t g, uint8_t b);

#endif