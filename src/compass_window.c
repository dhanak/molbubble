#include <pebble.h>
#include "mol_bubble.h"

enum { MAX_DISTANCE_LENGTH = 32 };
    
static Window *s_compass_window;
static GPath* s_compass_path;
static Layer *s_compass_layer;
static TextLayer *s_station_name_layer;
static TextLayer *s_calibration_layer;
static TextLayer *s_distance_layer;
static char s_distance_str[MAX_DISTANCE_LENGTH];
static const GPathInfo COMPASS_PATH_INFO = {
  .num_points = 4,
  .points = (GPoint []) {{0,-24}, {24,24}, {0,10}, {-24,24}}
};
static CompassHeading s_compass_start_angle, s_compass_angle, s_compass_target_angle;
static Animation* s_compass_animation = NULL;

static bool compass_visible()
{
    return window_is_loaded(s_compass_window);
}

static void compass_set_direction(Animation* animation, const AnimationProgress progress)
{
    CompassHeading diff = (s_compass_target_angle - s_compass_start_angle + 3*TRIG_MAX_ANGLE/2) % TRIG_MAX_ANGLE - TRIG_MAX_ANGLE/2;
    s_compass_angle = (s_compass_start_angle +
        diff*(progress-ANIMATION_NORMALIZED_MIN)/(ANIMATION_NORMALIZED_MAX-ANIMATION_NORMALIZED_MIN)) % TRIG_MAX_ANGLE;
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting compass angle to: %u", (unsigned int)s_compass_angle);
    gpath_rotate_to(s_compass_path, s_compass_angle);
    layer_mark_dirty(s_compass_layer);
}

static void update_compass_station_name()
{
    if (compass_visible())
    {
        text_layer_set_text(s_station_name_layer, s_selected_station->name);    
    }
}

static void update_compass_direction(CompassHeading heading)
{
    if (compass_visible())
    {
        s_compass_target_angle = (heading - s_selected_station->bearing + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
        stop_animation(&s_compass_animation);
        s_compass_start_angle = s_compass_angle;
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Scheduling compass animation from %u to %u",
                (unsigned int)s_compass_start_angle, (unsigned int)s_compass_target_angle);
        static const AnimationImplementation compass_animation = { .update = compass_set_direction };
        s_compass_animation = animation_create();
        animation_set_implementation(s_compass_animation, &compass_animation);
        animation_set_curve(s_compass_animation, AnimationCurveLinear);
        animation_schedule(s_compass_animation);
        /*       
        snprintf(s_distance_str, MAX_DISTANCE_LENGTH, "%d %ld %ld",
            s_selected_station->distance, heading, s_selected_station->bearing);
        text_layer_set_text(s_distance_layer, s_distance_str);
        */
    }
}

static void compass_hide_calibration_text(bool hidden)
{
    layer_set_hidden((Layer*)s_calibration_layer, hidden);
}

static void compass_handler(CompassHeadingData headingData)
{
    compass_hide_calibration_text(headingData.compass_status == CompassStatusCalibrated);
    update_compass_direction(headingData.true_heading);
}

static void compass_set_text_layer_properties(TextLayer *text_layer, const char *font_key)
{
    text_layer_set_font(text_layer, fonts_get_system_font(font_key));
    text_layer_set_background_color(text_layer, GColorClear);
    text_layer_set_text_color(text_layer, GColorBlack);
    text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(text_layer, GTextOverflowModeTrailingEllipsis);
}

static void compass_layer_update(Layer *layer, GContext* ctx)
{
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorRed);
    gpath_draw_filled(ctx, s_compass_path);
    graphics_context_set_stroke_color(ctx, GColorInchworm);
    graphics_context_set_stroke_width(ctx, 4);
    gpath_draw_outline(ctx, s_compass_path);
#else
    graphics_context_set_fill_color(ctx, GColorBlack);
    gpath_draw_filled(ctx, s_compass_path);
#endif
}

static void compass_window_load()
{
    Layer *window_layer = window_get_root_layer(s_compass_window);
    GRect bounds = layer_get_bounds(window_layer);
    
    // station name
    s_station_name_layer = text_layer_create(GRect(0, 0, 144, 52));
    compass_set_text_layer_properties(s_station_name_layer, FONT_KEY_GOTHIC_24_BOLD);
    layer_add_child(window_layer, text_layer_get_layer(s_station_name_layer));
    
    // compass
    s_compass_path = gpath_create(&COMPASS_PATH_INFO);
    gpath_move_to(s_compass_path, GPoint(40, 40));
    s_compass_layer = layer_create(GRect((bounds.size.w-80)/2, 60, 80, 80));
    layer_set_update_proc(s_compass_layer, compass_layer_update);
    layer_add_child(window_layer, bitmap_layer_get_layer((BitmapLayer*)s_compass_layer));
    
    // distance
    s_distance_layer = text_layer_create(GRect(0, bounds.size.h-26, 144, 24));
    compass_set_text_layer_properties(s_distance_layer, FONT_KEY_GOTHIC_24);
    layer_add_child(window_layer, text_layer_get_layer(s_distance_layer));
    
    // calibration text
    s_calibration_layer = text_layer_create(GRect(0, 0, 144, bounds.size.h));
    compass_set_text_layer_properties(s_calibration_layer, FONT_KEY_GOTHIC_24_BOLD);
    text_layer_set_background_color(s_calibration_layer, GColorWhite);
    text_layer_set_text(s_calibration_layer, "Compass is calibrating!\n\nMove your wrist around to aid calibration.");
    compass_hide_calibration_text(true);
    layer_add_child(window_layer, text_layer_get_layer(s_calibration_layer));
}

static void compass_window_appear()
{
    update_compass_station_name();
    update_compass_distance();
    s_compass_angle = 0;
    compass_service_subscribe(compass_handler);
}

static void compass_window_disappear()
{
    compass_service_unsubscribe();
}

static void compass_window_unload()
{
    stop_animation(&s_compass_animation);
    text_layer_destroy(s_calibration_layer);
    text_layer_destroy(s_distance_layer);
    layer_destroy(s_compass_layer);
    gpath_destroy(s_compass_path);
    text_layer_destroy(s_station_name_layer);
}

#ifdef PBL_PLATFORM_BASALT
static Animation *create_anim_scroll_out(Layer *layer, uint32_t duration, int16_t dy) {
  GPoint to_origin = GPoint(0, dy);
  Animation *result = (Animation *) property_animation_create_bounds_origin(layer, NULL, &to_origin);
  animation_set_duration(result, duration);
  animation_set_curve(result, AnimationCurveLinear);
  return result;
}

static Animation *create_anim_scroll_in(Layer *layer, uint32_t duration, int16_t dy) {
  GPoint from_origin = GPoint(0, dy);
  Animation *result = (Animation *) property_animation_create_bounds_origin(layer, &from_origin, &GPointZero);
  animation_set_duration(result, duration);
  animation_set_curve(result, AnimationCurveEaseOut);
  return result;
}

static void update_compass_texts(Animation* animation, bool finished, void* context)
{
    update_compass_station_name();
    update_compass_distance();
}
#endif

static void compass_window_button_handler(ClickRecognizerRef recognizer, void *context)
{
    bool up = click_recognizer_get_button_id(recognizer) == BUTTON_ID_UP;
    MenuIndex index = menu_get_selection();
    bool ok = up ? index.row > 0 : index.row+1 < s_stations_size;
    if (ok)
    {
        index.row += up ? -1 : 1;
        menu_set_selection(index, false);
    }
#ifdef PBL_PLATFORM_BASALT
    int16_t y = (up ? 1 : -1) * (ok ? 52 : 6);
    uint32_t d = ok ? 200 : 100;
    Animation* out_station = create_anim_scroll_out(text_layer_get_layer(s_station_name_layer), d, y);
    Animation* out_distance = create_anim_scroll_out(text_layer_get_layer(s_distance_layer), d, y);
    Animation* anim_out = animation_spawn_create(out_station, out_distance, NULL);
    if (ok)
    {
        animation_set_handlers(anim_out, (AnimationHandlers){ .stopped = update_compass_texts }, NULL);
        y = -y;
    }
    Animation* in_station = create_anim_scroll_in(text_layer_get_layer(s_station_name_layer), d, y);
    Animation* in_distance = create_anim_scroll_in(text_layer_get_layer(s_distance_layer), d, y);
    Animation* anim_in = animation_spawn_create(in_station, in_distance, NULL);
    animation_schedule(animation_sequence_create(anim_out, anim_in, NULL));
#else
    update_compass_station_name();
    update_compass_distance();
#endif
}

static void compass_window_click_config_provider()
{
    window_single_click_subscribe(BUTTON_ID_UP, compass_window_button_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, compass_window_button_handler);
}

////////////////   E X P O R T E D   F U N C T I O N S   ////////////////

void compass_window_init()
{
    s_compass_window = window_create();
#ifdef PBL_PLATFORM_APLITE
    window_set_fullscreen(s_compass_window, true);
#endif
    window_set_background_color(s_compass_window, GColorWhite);
    window_set_window_handlers(s_compass_window, (WindowHandlers) {
        .load = compass_window_load,
        .appear = compass_window_appear,
        .disappear = compass_window_disappear,
        .unload = compass_window_unload
    });
    window_set_click_config_provider(s_compass_window, compass_window_click_config_provider);
}

void compass_window_deinit()
{
    window_destroy(s_compass_window);
}

void push_compass_window()
{
    window_stack_push(s_compass_window, true);
}

void update_compass_distance()
{
    if (compass_visible())
    {
        snprintf(s_distance_str, MAX_DISTANCE_LENGTH, "%d meters", s_selected_station->distance);
        text_layer_set_text(s_distance_layer, s_distance_str);
    }
}
