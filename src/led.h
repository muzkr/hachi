#ifndef _LED_H
#define _LED_H

#include "hardware/gpio.h"

#define LED_PIN PICO_DEFAULT_LED_PIN

static inline void led_on(bool on) { gpio_put(LED_PIN, on); }
static inline void led_toggle() { gpio_put(LED_PIN, !gpio_get_out_level(LED_PIN)); }

void led_config();

#endif // _LED_H
