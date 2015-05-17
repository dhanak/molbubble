#include <pebble.h>
#include "mol_bubble.h"

enum { MAX_DISTANCE_LENGTH = 32 };
static const GPathInfo COMPASS_PATH_INFO = {
  .num_points = 4,
  .points = (GPoint []) {{0,-24}, {24,24}, {0,10}, {-24,24}}
};

static Window *p_window;
static GPath* p_compass_path;
static Layer *p_compass_layer;
static TextLayer *p_station_name_layer;
static TextLayer *p_calibration_layer;
static TextLayer *p_distance_layer;
static char p_distance_str[MAX_DISTANCE_LENGTH];
static CompassHeading n_compass_start_angle, n_compass_angle, n_compass_target_angle;
static Animation* p_compass_animation = NULL;

static bool is_visible()
{
    return window_is_loaded(p_window);
}

static void set_compass_direction(Animation* animation, const AnimationProgress progress)
{
    CompassHeading diff = (n_compass_target_angle - n_compass_start_angle + 3*TRIG_MAX_ANGLE/2) % TRIG_MAX_ANGLE - TRIG_MAX_ANGLE/2;
    n_compass_angle = (n_compass_start_angle +
        diff*(progress-ANIMATION_NORMALIZED_MIN)/(ANIMATION_NORMALIZED_MAX-ANIMATION_NORMALIZED_MIN)) % TRIG_MAX_ANGLE;
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting compass angle to: %u", (unsigned int)n_compass_angle);
    gpath_rotate_to(p_compass_path, n_compass_angle);
    layer_mark_dirty(p_compass_layer);
}

static void update_compass_direction(CompassHeading heading)
{
    if (is_visible())
    {
        n_compass_target_angle = (heading - s_selected_station->bearing + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
        stop_animation(&p_compass_animation);
        n_compass_start_angle = n_compass_angle;
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Scheduling compass animation from %u to %u",
                (unsigned int)n_compass_start_angle, (unsigned int)n_compass_target_angle);
        static const AnimationImplementation compass_animation = { .update = set_compass_direction };
        p_compass_animation = animation_create();
        animation_set_implementation(p_compass_animation, &compass_animation);
        animation_set_curve(p_compass_animation, AnimationCurveLinear);
        animation_schedule(p_compass_animation);
        /*       
        snprintf(p_distance_str, MAX_DISTANCE_LENGTH, "%d %ld %ld",
            s_selected_station->distance, heading, s_selected_station->bearing);
        text_layer_set_text(p_distance_layer, p_distance_str);
        */
    }
}

static void update_station_name()
{
    if (is_visible())
    {
        text_layer_set_text(p_station_name_layer, s_selected_station->name);    
    }
}

static void set_calibration_text_visibility(bool visible)
{
    layer_set_hidden((Layer*)p_calibration_layer, !visible);
}

static void compass_handler(CompassHeadingData headingData)
{
    set_calibration_text_visibility(headingData.compass_status != CompassStatusCalibrated);
    update_compass_direction(headingData.true_heading);
}

static void set_text_layer_properties(TextLayer *text_layer, const char *font_key)
{
    text_layer_set_font(text_layer, fonts_get_system_font(font_key));
    text_layer_set_background_color(text_layer, GColorClear);
    text_layer_set_text_color(text_layer, GColorBlack);
    text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(text_layer, GTextOverflowModeTrailingEllipsis);
}

static void layer_update(Layer *layer, GContext* ctx)
{
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorRed);
    gpath_draw_filled(ctx, p_compass_path);
    graphics_context_set_stroke_color(ctx, GColorInchworm);
    graphics_context_set_stroke_width(ctx, 4);
    gpath_draw_outline(ctx, p_compass_path);
#else
    graphics_context_set_fill_color(ctx, GColorBlack);
    gpath_draw_filled(ctx, p_compass_path);
#endif
}

static void window_load()
{
    Layer *window_layer = window_get_root_layer(p_window);
    GRect bounds = layer_get_bounds(window_layer);
    
    // station name
    p_station_name_layer = text_layer_create(GRect(0, 0, 144, 52));
    set_text_layer_properties(p_station_name_layer, FONT_KEY_GOTHIC_24_BOLD);
    layer_add_child(window_layer, text_layer_get_layer(p_station_name_layer));
    
    // compass
    p_compass_path = gpath_create(&COMPASS_PATH_INFO);
    gpath_move_to(p_compass_path, GPoint(40, 40));
    p_compass_layer = layer_create(GRect((bounds.size.w-80)/2, 60, 80, 80));
    layer_set_update_proc(p_compass_layer, layer_update);
    layer_add_child(window_layer, bitmap_layer_get_layer((BitmapLayer*)p_compass_layer));
    
    // distance
    p_distance_layer = text_layer_create(GRect(0, bounds.size.h-26, 144, 24));
    set_text_layer_properties(p_distance_layer, FONT_KEY_GOTHIC_24);
    layer_add_child(window_layer, text_layer_get_layer(p_distance_layer));
    
    // calibration text
    p_calibration_layer = text_layer_create(GRect(0, 0, 144, bounds.size.h));
    set_text_layer_properties(p_calibration_layer, FONT_KEY_GOTHIC_24_BOLD);
    text_layer_set_background_color(p_calibration_layer, GColorWhite);
    text_layer_set_text(p_calibration_layer, "Compass is calibrating!\n\nMove your wrist around to aid calibration.");
    set_calibration_text_visibility(false);
    layer_add_child(window_layer, text_layer_get_layer(p_calibration_layer));
}

static void window_appear()
{
    update_station_name();
    compass_window__update_distance();
    n_compass_angle = 0;
    compass_service_subscribe(compass_handler);
}

static void window_disappear()
{
    compass_service_unsubscribe();
}

static void window_unload()
{
    stop_animation(&p_compass_animation);
    text_layer_destroy(p_calibration_layer);
    text_layer_destroy(p_distance_layer);
    layer_destroy(p_compass_layer);
    gpath_destroy(p_compass_path);
    text_layer_destroy(p_station_name_layer);
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

static void update_texts(Animation* animation, bool finished, void* context)
{
    update_station_name();
    compass_window__update_distance();
}
#endif

static void button_handler(ClickRecognizerRef recognizer, void *context)
{
    bool up = click_recognizer_get_button_id(recognizer) == BUTTON_ID_UP;
    MenuIndex index = station_menu__get_selection();
    bool ok = up ? index.row > 0 : index.row+1 < s_stations_size;
    if (ok)
    {
        index.row += up ? -1 : 1;
        station_menu__set_selection(index, false);
    }
#ifdef PBL_PLATFORM_BASALT
    int16_t y = (up ? 1 : -1) * (ok ? 52 : 6);
    uint32_t d = ok ? 200 : 100;
    Animation* out_station = create_anim_scroll_out(text_layer_get_layer(p_station_name_layer), d, y);
    Animation* out_distance = create_anim_scroll_out(text_layer_get_layer(p_distance_layer), d, y);
    Animation* anim_out = animation_spawn_create(out_station, out_distance, NULL);
    if (ok)
    {
        animation_set_handlers(anim_out, (AnimationHandlers){ .stopped = update_texts }, NULL);
        y = -y;
    }
    Animation* in_station = create_anim_scroll_in(text_layer_get_layer(p_station_name_layer), d, y);
    Animation* in_distance = create_anim_scroll_in(text_layer_get_layer(p_distance_layer), d, y);
    Animation* anim_in = animation_spawn_create(in_station, in_distance, NULL);
    animation_schedule(animation_sequence_create(anim_out, anim_in, NULL));
#else
    update_station_name();
    compass_window__update_distance();
#endif
}

static void click_config_provider()
{
    window_single_click_subscribe(BUTTON_ID_UP, button_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, button_handler);
}

////////////////   E X P O R T E D   F U N C T I O N S   ////////////////

void compass_window__init()
{
    p_window = window_create();
#ifdef PBL_PLATFORM_APLITE
    window_set_fullscreen(p_window, true);
#endif
    window_set_background_color(p_window, GColorWhite);
    window_set_window_handlers(p_window, (WindowHandlers) {
        .load = window_load,
        .appear = window_appear,
        .disappear = window_disappear,
        .unload = window_unload
    });
    window_set_click_config_provider(p_window, click_config_provider);
}

void compass_window__deinit()
{
    window_destroy(p_window);
}

void compass_window__show()
{
    window_stack_push(p_window, true);
}

void compass_window__update_distance()
{
    if (is_visible())
    {
        snprintf(p_distance_str, MAX_DISTANCE_LENGTH, "%d meters", s_selected_station->distance);
        text_layer_set_text(p_distance_layer, p_distance_str);
    }
}
