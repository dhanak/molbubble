#pragma once

#include <pebble.h>

uint16_t sqrt32(uint32_t n);
void stop_animation(Animation** anim);
void stop_property_animation(PropertyAnimation** prop_anim);
void text_layer_set_properties(TextLayer *text_layer,
                               const char *font_key,
                               GColor bg_color,
                               GColor text_color,
                               GTextAlignment alignment);
