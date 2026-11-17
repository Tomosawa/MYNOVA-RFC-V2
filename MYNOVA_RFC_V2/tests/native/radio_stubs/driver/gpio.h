#pragma once
#include <Arduino.h>
using gpio_num_t = int;
inline int gpio_get_level(gpio_num_t pin) { return pinLevels[pin]; }

#include <SPI.h>
#include <driver/rmt.h>
struct GpioEdge {uint64_t time;int level;bool tx;};
inline std::vector<GpioEdge> gpioEdges;
inline int gpio_set_level(gpio_num_t pin,int level) {
    assert(pinModes[pin]==OUTPUT);pinLevels[pin]=level;
    gpioEdges.push_back({radioClockUs,level,SPI.marc==0x13});return ESP_OK;
}
