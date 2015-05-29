#include <pebble.h>
#include "utils.h"

uint16_t sqrt32(uint32_t n)
{   // https://www.quora.com/Which-is-the-fastest-algorithm-to-compute-integer-square-root-of-a-number
    uint32_t c = 0x8000;
    uint32_t g = 0x8000;
  
    for (;;)
    {  
        if (g*g > n)
            g ^= c;
        c >>= 1;  
        if (c == 0)
            return g;
        g |= c;  
    }      
}

void stop_animation(Animation** anim)
{
    if (*anim)
    {
        animation_unschedule(*anim);
#ifdef PBL_PLATFORM_APLITE
        animation_destroy(*anim);
#endif
        *anim = NULL;
    }
}

void stop_property_animation(PropertyAnimation** prop_anim)
{
    if (*prop_anim)
    {
        animation_unschedule(property_animation_get_animation(*prop_anim));
#ifdef PBL_PLATFORM_APLITE
        property_animation_destroy(*prop_anim);
#endif
        *prop_anim = NULL;
    }    
}

void text_layer_set_properties(TextLayer *text_layer,
                               const char *font_key,
                               GColor bg_color,
                               GColor text_color,
                               GTextAlignment alignment)
{
    text_layer_set_font(text_layer, fonts_get_system_font(font_key));
    text_layer_set_background_color(text_layer, bg_color);
    text_layer_set_text_color(text_layer, text_color);
    text_layer_set_text_alignment(text_layer, alignment);
    text_layer_set_overflow_mode(text_layer, GTextOverflowModeTrailingEllipsis);
}
