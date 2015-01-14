#include <pebble.h>

// Key values for AppMessage Dictionary
enum
{
    KEY_X,
    KEY_Y,
    KEY_NUM_STATIONS,
    KEY_INDEX,
    KEY_NAME,
    KEY_RACKS,
    KEY_UPDATE,
};

// other constants
enum
{
    MAX_STATION_NAME_LENGTH = 32,
    MAX_DISTANCE_LENGTH     = 32,
    ICON_LAYER_HEIGHT       = 36,
};

typedef struct Pending
{
    int stations;
    bool location;
    bool bikes;
} Pending;
Pending s_pending = { INT32_MAX, true, true };

// stations
typedef struct Coordinates
{
    int16_t x, y;
} Coordinates;
Coordinates s_last_known_coords = { 0, 0 };

typedef struct Station
{
    char name[MAX_STATION_NAME_LENGTH];
    Coordinates coords; // coordinates relative to city center, in meters
    uint8_t racks; // number of racks
    uint8_t bikes; // number of bikes
    uint16_t distance; // distance from last known coordinate, in meters
    CompassHeading bearing; // bearing from last known coordinate, in meters
} Station;
enum { STATION_PERSIST_SIZE = sizeof(Station)-sizeof(uint8_t)-sizeof(uint16_t)-sizeof(CompassHeading) };
int s_stations_size = 0;
Station *s_stations = NULL;
Station **s_sorted_stations = NULL;
Station *s_selected_station = NULL; // pointer to selected station

// main window UI elements
Window *s_main_window;
MenuLayer *s_menu_layer;
PropertyAnimation *s_menu_animation = NULL;
MenuLayerCallbacks s_menu_callbacks;

Layer *s_icon_layer;
enum { ICON_STATIONS, ICON_BIKES, ICON_LOCATION, ICON_DITHER, ICON_COUNT };
GBitmap *s_icons[ICON_COUNT] = { NULL };
PropertyAnimation *s_icon_animation = NULL;

// compass window UI elements
Window *s_compass_window;
GBitmap* s_compass_bitmap;
RotBitmapLayer *s_compass_layer;
TextLayer *s_station_name_layer;
TextLayer *s_calibration_layer;
TextLayer *s_distance_layer;
InverterLayer *s_inverter_layer;
char s_distance_str[MAX_DISTANCE_LENGTH];

// main window functions

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

void reallocate_stations(int size)
{
    if (s_stations_size == size)
    {
        if (s_pending.stations && size)
        {   // if some are pending, all are pending
            s_pending.stations = size;
        }
        return;
    }
    
    free(s_stations);
    free(s_sorted_stations);
    s_stations_size = size;
    s_stations = calloc(s_stations_size, sizeof(Station));
    s_sorted_stations = calloc(s_stations_size, sizeof(Station*));
    for (int i = 0; i < size; i++)
    {
        s_sorted_stations[i] = &s_stations[i];
    }
    s_pending.stations = size;
}

void swap_stations(int a, int b)
{
    Station* tmp = s_sorted_stations[a];
    s_sorted_stations[a] = s_sorted_stations[b];
    s_sorted_stations[b] = tmp;
}

void sort_stations(int start, int end)
{
    if (!s_pending.location && end > start)
    {
        int pivot_index = (start + end) / 2;
        Station* pivot = s_sorted_stations[pivot_index];
        swap_stations(pivot_index, end);
        int chg;
        for(int i = chg = start; i < end; i++)
        {
            if(s_sorted_stations[i]->name[0] && s_sorted_stations[i]->distance < pivot->distance)
            {
                swap_stations(i, chg++);
            }
        }
        swap_stations(chg, end);
        
        sort_stations(start, chg-1);
        sort_stations(chg+1, end);
    }
}

void update_station(Station *station)
{
    if (!s_pending.location)
    {
        int32_t dx = station->coords.x - s_last_known_coords.x;
        int32_t dy = station->coords.y - s_last_known_coords.y;
        station->distance = sqrt32(dx*dx + dy*dy);
        station->bearing = atan2_lookup(dx, -dy);
    }
}

void update_stations()
{
    if (!s_pending.stations)
    {
        for (int i = 0; i < s_stations_size; i++)
        {
            update_station(&s_stations[i]);
        }
        sort_stations(0, s_stations_size-1);
    }
}

void persist_write_stations()
{
    persist_write_int(0, s_stations_size);
    for (int i = 0; i < s_stations_size; i++)
    {
        if (s_stations[i].name[0])
        {
            persist_write_data(i+1, &s_stations[i], STATION_PERSIST_SIZE);
        }
    }
}

void persist_read_stations()
{
    int size = persist_read_int(0);
    persist_delete(0);
    reallocate_stations(size);
    for (int i = 0; i < size; i++)
    {
        persist_read_data(i+1, &s_stations[i], STATION_PERSIST_SIZE);
        persist_delete(i+1);
        if (s_stations[i].name[0])
        {
            s_pending.stations--;
        }
    }
}

void copy_name(char *dst, const char *src)
{
    int l = strlen(src);
    if (l+1 < MAX_STATION_NAME_LENGTH)
    {
        strcpy(dst, src);
    }
    else
    {
        l = MAX_STATION_NAME_LENGTH-4; // ellipsis + terminating null
        while (src[l] & 0x80)
            l--; // find last ASCII char
        strncpy(dst, src, l);
        dst[l++] = 0xE2; // horizontal ellipsis
        dst[l++] = 0x80;
        dst[l++] = 0xA6;
        dst[l] = '\0';
    }
}

void stop_menu_animations()
{
    if (s_menu_animation)
    {
        animation_unschedule((Animation*)s_menu_animation);
        property_animation_destroy(s_menu_animation);
        s_menu_animation = NULL;
    }
    if (s_icon_animation)
    {
        animation_unschedule((Animation*)s_icon_animation);
        property_animation_destroy(s_icon_animation);
        s_icon_animation = NULL;
    }
}

void resize_menu(GRect *frame)
{
    GRect current_frame = layer_get_frame((Layer*)s_menu_layer);
    if (!grect_equal(frame, &current_frame))
    {
        stop_menu_animations();
        GRect icon_frame = GRect(0, 0, 144, frame->origin.y);
        s_menu_animation = property_animation_create_layer_frame((Layer*)s_menu_layer, &current_frame, frame);
        s_icon_animation = property_animation_create_layer_frame(s_icon_layer, NULL, &icon_frame);
        animation_schedule((Animation*)s_menu_animation);
        animation_schedule((Animation*)s_icon_animation);
    }
}

void refresh_icons()
{
    layer_mark_dirty(s_icon_layer);
}

void refresh_menu()
{
    menu_layer_reload_data(s_menu_layer);

    GRect frame = layer_get_bounds(window_get_root_layer(s_main_window));
    if (s_pending.stations || s_pending.location || s_pending.bikes)
    {
        frame.size.h -= ICON_LAYER_HEIGHT;
        frame.origin.y += ICON_LAYER_HEIGHT;
    }
    resize_menu(&frame);
    
    // update selection
    MenuIndex selection = menu_layer_get_selected_index(s_menu_layer);
    if (s_selected_station && s_selected_station != s_sorted_stations[selection.row])
    {   // find selected station in reordered list
        for (int i = 0; i < s_stations_size; i++)
        {
            if (s_selected_station == s_sorted_stations[i])
            {
                selection.row = i;
                break;
            }
        }
        menu_layer_set_selected_index(s_menu_layer, selection, MenuRowAlignCenter, true);
    }
}

// compass window functions

bool compass_visible()
{
    return window_is_loaded(s_compass_window);
}

void update_compass_station_name()
{
    if (compass_visible())
    {
        text_layer_set_text(s_station_name_layer, s_selected_station->name);    
    }
}

void update_compass_distance()
{
    if (compass_visible())
    {
        snprintf(s_distance_str, MAX_DISTANCE_LENGTH, "%d meters", s_selected_station->distance);
        text_layer_set_text(s_distance_layer, s_distance_str);
    }
}

void update_compass_direction(CompassHeading heading)
{
    if (compass_visible())
    {
        CompassHeading angle = (heading - s_selected_station->bearing + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
        rot_bitmap_layer_set_angle(s_compass_layer, angle);
        /*       
        snprintf(s_distance_str, MAX_DISTANCE_LENGTH, "%d %ld %ld",
            s_selected_station->distance, heading, s_selected_station->bearing);
        text_layer_set_text(s_distance_layer, s_distance_str);
        */
    }
}


// callbacks

void send_request()
{
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_end(iter);
    app_message_outbox_send();
}

void inbox_received_callback(DictionaryIterator *iterator, void *context)
{
    Tuple *t;
    
    if ((t = dict_find(iterator, KEY_NUM_STATIONS)) != NULL)
    {   // station publish/update begins, allocate vector (if necesary)
        reallocate_stations(t->value->int32);
        refresh_menu();
    }
    else if ((t = dict_find(iterator, KEY_INDEX)) != NULL)
    {   // station publish package
        int i = t->value->int32;
        if (i < s_stations_size)
        {
            Station *station = &s_stations[i];
            t = dict_read_first(iterator);
            while (t != NULL)
            {
                switch (t->key)
                {
                case KEY_NAME:
                    copy_name(station->name, t->value->cstring);
                    break;
                case KEY_X:
                    station->coords.x = t->value->int32;
                    break;
                case KEY_Y:
                    station->coords.y = t->value->int32;
                    break;
                case KEY_RACKS:
                    station->racks = t->value->int32;
                }
                t = dict_read_next(iterator);
            }
            if (s_pending.stations)
            {
                s_pending.stations--;
                refresh_icons();
            }
            if (i == s_stations_size-1)
            {   // received last refresh, update
                update_stations();
            }
            // update display
            refresh_menu();
            if (station == s_selected_station)
            {
                update_station(station);
                update_compass_distance();
            }
        }
    }
    else if ((t = dict_find(iterator, KEY_UPDATE)) != NULL)
    {   // station update package
        for (int start = t->value->data[0], i = 1; i < t->length && start+i-1 < s_stations_size; i++)
        {
            s_stations[start+i-1].bikes = t->value->data[i];
        }
        s_pending.bikes = false;
        // update display
        refresh_icons();
        refresh_menu();
    }
    else
    {   // position update package
        t = dict_read_first(iterator);
        while (t != NULL)
        {
            switch (t->key)
            {
                case KEY_X:
                    s_last_known_coords.x = t->value->int32;
                    break;
                case KEY_Y:
                    s_last_known_coords.y = t->value->int32;
                    break;
            }
            t = dict_read_next(iterator);
        }
        s_pending.location = false;
        update_stations();
        // update display
        refresh_icons();
        refresh_menu();
        update_compass_distance();
    }
}

void inbox_dropped_callback(AppMessageResult reason, void *context)
{
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context)
{
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

void outbox_sent_callback(DictionaryIterator *iterator, void *context)
{
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

void icon_layer_update(Layer *layer, GContext *ctx)
{
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    GRect rect = GRect(10, 2, 32, 32);
    graphics_draw_bitmap_in_rect(ctx, s_icons[ICON_STATIONS], rect);
    rect.origin.x += 42;
    graphics_draw_bitmap_in_rect(ctx, s_icons[ICON_BIKES], rect);
    rect.origin.x += 42;
    graphics_draw_bitmap_in_rect(ctx, s_icons[ICON_LOCATION], rect);
    
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    rect.origin.x = 10;
    if (s_pending.stations)
    {
        rect.size.h = s_stations_size ? 32 * s_pending.stations/s_stations_size : 32;
        graphics_draw_bitmap_in_rect(ctx, s_icons[ICON_DITHER], rect);
        rect.size.h = 32;
    }
    rect.origin.x += 42;
    if (s_pending.bikes)
    {
        graphics_draw_bitmap_in_rect(ctx, s_icons[ICON_DITHER], rect);
    }
    rect.origin.x += 42;
    if (s_pending.location)
    {
        graphics_draw_bitmap_in_rect(ctx, s_icons[ICON_DITHER], rect);
    }
}

uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *callback_context)
{
    return s_stations_size;
}

void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context)
{
    Station *station = s_sorted_stations[cell_index->row];
    char buf[64] = { 0 };
    char *p = buf;
    if (!s_pending.stations && !s_pending.location)
    {
        p += snprintf(p, buf+64-p, "%dm, ", station->distance);
    }
    if (!s_pending.bikes)
    {
        p += snprintf(p, buf+64-p, "%d bikes", station->bikes);
    }
    if (!s_pending.stations)
    {
        if (station->bikes) *p++ = '/';
        p += snprintf(p, buf+64-p, "%d racks", station->racks);
    }
    else if (p == buf)
    {   // add ellipsis in empty buffer
        *p++ = 0xE2;
        *p++ = 0x80;
        *p++ = 0xA6;
    }
    menu_cell_basic_draw(ctx, cell_layer, station->name[0] ? station->name : "\xe2\x80\xa6", buf, NULL);
}

void menu_selection_changed(MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context)
{
    s_selected_station = s_sorted_stations[new_index.row];
}

void menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    if (!s_pending.stations && !s_pending.location)
    {
        s_selected_station = s_sorted_stations[cell_index->row];  // just in case no row is selected yet
        window_stack_push(s_compass_window, true);
    }
}

void menu_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    send_request();
}

void compass_hide_calibration_text(bool hidden)
{
    layer_set_hidden((Layer*)s_calibration_layer, hidden);
}

void compass_handler(CompassHeadingData headingData)
{
    compass_hide_calibration_text(headingData.compass_status != CompassStatusDataInvalid);
    update_compass_direction(headingData.true_heading);
}

void compass_set_text_layer_properties(TextLayer *text_layer, const char *font_key)
{
    text_layer_set_font(text_layer, fonts_get_system_font(font_key));
    text_layer_set_background_color(text_layer, GColorClear);
    text_layer_set_text_color(text_layer, GColorWhite);
    text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(text_layer, GTextOverflowModeTrailingEllipsis);
}

void compass_window_load()
{
    Layer *window_layer = window_get_root_layer(s_compass_window);
    GRect bounds = layer_get_bounds(window_layer);
    
    // station name
    s_station_name_layer = text_layer_create(GRect(0, 0, 144, 52));
    compass_set_text_layer_properties(s_station_name_layer, FONT_KEY_GOTHIC_24_BOLD);
    layer_add_child(window_layer, text_layer_get_layer(s_station_name_layer));
    
    // compass
    s_compass_bitmap = gbitmap_create_with_resource(RESOURCE_ID_COMPASS);
    s_compass_layer = rot_bitmap_layer_create(s_compass_bitmap);
    bitmap_layer_set_bitmap((BitmapLayer*)s_compass_layer, s_compass_bitmap);
    GRect compass_frame = layer_get_frame((Layer*)s_compass_layer);
    compass_frame.origin.x = (bounds.size.w-compass_frame.size.w)/2;
    compass_frame.origin.y = 60;
    layer_set_frame((Layer*)s_compass_layer, compass_frame);
    layer_add_child(window_layer, bitmap_layer_get_layer((BitmapLayer*)s_compass_layer));
    
    // distance
    s_distance_layer = text_layer_create(GRect(0, bounds.size.h-26, 144, 24));
    compass_set_text_layer_properties(s_distance_layer, FONT_KEY_GOTHIC_24);
    layer_add_child(window_layer, text_layer_get_layer(s_distance_layer));
    
    // calibration text
    s_calibration_layer = text_layer_create(GRect(0, 0, 144, bounds.size.h));
    compass_set_text_layer_properties(s_calibration_layer, FONT_KEY_GOTHIC_24_BOLD);
    text_layer_set_background_color(s_calibration_layer, GColorBlack);
    text_layer_set_text(s_calibration_layer, "Compass is calibrating!\n\nMove your wrist around to aid calibration.");
    compass_hide_calibration_text(true);
    layer_add_child(window_layer, text_layer_get_layer(s_calibration_layer));
    
    s_inverter_layer = NULL;
    if (watch_info_get_color() == WATCH_INFO_COLOR_WHITE)
    {
        s_inverter_layer = inverter_layer_create(bounds);
        layer_add_child(window_layer, inverter_layer_get_layer(s_inverter_layer));
    }
}

void compass_window_appear()
{
    update_compass_station_name();
    update_compass_distance();
    compass_service_subscribe(compass_handler);
}

void compass_window_disappear()
{
    compass_service_unsubscribe();
}

void compass_window_unload()
{
    inverter_layer_destroy(s_inverter_layer);
    text_layer_destroy(s_calibration_layer);
    text_layer_destroy(s_distance_layer);
    rot_bitmap_layer_destroy(s_compass_layer);
    gbitmap_destroy(s_compass_bitmap);
    text_layer_destroy(s_station_name_layer);
}

void compass_window_button_handler(ClickRecognizerRef recognizer, void *context)
{
    bool up = click_recognizer_get_button_id(recognizer) == BUTTON_ID_UP;
    menu_layer_set_selected_next(s_menu_layer, up, MenuRowAlignCenter, false);
    update_compass_station_name();
    update_compass_distance();
}

void compass_window_click_config_provider()
{
    window_single_click_subscribe(BUTTON_ID_UP, compass_window_button_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, compass_window_button_handler);
}

// main functions
void init(void)
{
    // read persistence data
    persist_read_stations();
    
    s_icons[ICON_STATIONS] = gbitmap_create_with_resource(RESOURCE_ID_MAP);
    s_icons[ICON_BIKES]    = gbitmap_create_with_resource(RESOURCE_ID_BIKE);
    s_icons[ICON_LOCATION] = gbitmap_create_with_resource(RESOURCE_ID_LOCATION);
    s_icons[ICON_DITHER]   = gbitmap_create_with_resource(RESOURCE_ID_DITHER);

    // create main window
    s_main_window = window_create();
    window_stack_push(s_main_window, true);
    Layer *window_layer = window_get_root_layer(s_main_window);
    
    s_icon_layer = layer_create(GRect(0, 0, 144, ICON_LAYER_HEIGHT));
    layer_set_update_proc(s_icon_layer, icon_layer_update);
    layer_add_child(window_layer, s_icon_layer);

    s_menu_callbacks.get_num_rows = menu_get_num_rows;
    s_menu_callbacks.draw_row = menu_draw_row;
    s_menu_callbacks.selection_changed = menu_selection_changed;
    s_menu_callbacks.select_click = menu_select_click;
    s_menu_callbacks.select_long_click = menu_select_long_click;
    
    GRect bounds = layer_get_bounds(window_layer);
    bounds.origin.y += ICON_LAYER_HEIGHT;
    bounds.size.h -= ICON_LAYER_HEIGHT;
    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_callbacks);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
    menu_layer_set_click_config_onto_window(s_menu_layer, s_main_window);
    
    // create compass window
    s_compass_window = window_create();
    window_set_background_color(s_compass_window, GColorBlack);
    window_set_window_handlers(s_compass_window, (WindowHandlers) {
        .load = compass_window_load,
        .appear = compass_window_appear,
        .disappear = compass_window_disappear,
        .unload = compass_window_unload
    });
    window_set_click_config_provider(s_compass_window, compass_window_click_config_provider);

    // register AppMessage handlers
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);

    app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

void deinit(void)
{
    stop_menu_animations();
    app_message_deregister_callbacks();
    window_destroy(s_compass_window);
    menu_layer_destroy(s_menu_layer);
    layer_destroy(s_icon_layer);
    window_destroy(s_main_window);

    for (int i = 0; i < ICON_COUNT; i++)
    {
        gbitmap_destroy(s_icons[i]);
    }
    
    persist_write_stations();
    free(s_stations);
    free(s_sorted_stations);
}

int main(void)
{
    init();
    app_event_loop();
    deinit();
}
