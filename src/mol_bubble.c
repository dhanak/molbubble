#include <pebble.h>

enum
{
    MAX_STATION_NAME_LENGTH = 64,
    MAX_DISTANCE_LENGTH = 32
};

Window *s_main_window;
MenuLayer *s_menu_layer;
MenuLayerCallbacks s_menu_callbacks;

Window *s_compass_window;
GBitmap* s_compass_bitmap;
RotBitmapLayer *s_compass_layer;
TextLayer *s_station_name_layer;
TextLayer *s_distance_layer;
char s_distance_str[MAX_DISTANCE_LENGTH];

typedef struct Station
{
    char name[MAX_STATION_NAME_LENGTH];
    uint16_t distance;
    CompassHeading heading;
    uint8_t bikes;
    uint8_t racks;
    uint16_t uid;
} Station;
Station *s_stations = NULL;
Station *s_selected_station = NULL;
int s_stations_length = 0;
int s_number_of_stations = -1;

// Key values for AppMessage Dictionary
enum
{
    KEY_NUM_STATIONS = 0,
    KEY_NAME,
    KEY_DISTANCE,
    KEY_BIKES,
    KEY_RACKS,
    KEY_HEADING,
    KEY_UID,
};

void update_compass_distance()
{
    snprintf(s_distance_str, MAX_DISTANCE_LENGTH, "%d meters", s_selected_station->distance);
    text_layer_set_text(s_distance_layer, s_distance_str);
}

void update_compass_direction(CompassHeading heading)
{
    CompassHeading angle = (heading - s_selected_station->heading + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
    rot_bitmap_layer_set_angle(s_compass_layer, angle);
}

// Called when a message is received from PebbleKitJS
void inbox_received_callback(DictionaryIterator *iterator, void *context)
{
    Tuple *t = dict_read_first(iterator);
    while(t != NULL)
    {
        Station *station = s_selected_station;
        if (!station && s_stations)
        {
            station = &s_stations[s_number_of_stations];
        }
        
        switch (t->key)
        {
        case KEY_NUM_STATIONS:
            free(s_stations);
            s_number_of_stations = -1;
            s_stations_length = t->value->int32;
            s_stations = calloc(s_stations_length, sizeof(Station));
            break;
        case KEY_NAME:
            if (station) strncpy(station->name, t->value->cstring, MAX_STATION_NAME_LENGTH);
            break;
        case KEY_DISTANCE:
            if (station) station->distance = t->value->int32;
            if (station == s_selected_station)
            {
                update_compass_distance();
            }
            break;
        case KEY_BIKES:
            if (station) station->bikes = t->value->int32;
            break;
        case KEY_RACKS:
            if (station) station->racks = t->value->int32;
            break;
        case KEY_HEADING:
            if (station) station->heading = ((t->value->int32 + 360) % 360)*TRIG_MAX_ANGLE/360;
            break;
        case KEY_UID:
            if (station) station->uid = t->value->int32;
        }
        t = dict_read_next(iterator);
    }
    
    if (!s_selected_station)
    {   // menu is being updated
        if (s_number_of_stations++ == menu_layer_get_selected_index(s_menu_layer).row)
        {   // if selected item is updated, turn on lights
            light_enable_interaction();
        }
        menu_layer_reload_data(s_menu_layer);
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

uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *callback_context)
{
    return s_stations_length == 0 ? 1 : s_stations_length;
}

void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context)
{
    if (!s_stations)
    {
        menu_cell_basic_draw(ctx, cell_layer, "Loading...", NULL, NULL);
    }
    else if (cell_index->row < s_number_of_stations)
    {   // row already loaded
        char buf[64];
        Station *station = &s_stations[cell_index->row];
        snprintf(buf, 64, "%dm, %d bikes/%d racks", station->distance, station->bikes, station->racks);
        menu_cell_basic_draw(ctx, cell_layer, station->name, buf, NULL);
    }
    else
    {   // row data still loading
        menu_cell_basic_draw(ctx, cell_layer, "...", NULL, NULL);
    }
}

void send_uid(int uid)
{
    DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
    dict_write_int32(iter, KEY_UID, uid);
	dict_write_end(iter);
  	app_message_outbox_send();
}

void menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d clicked!", cell_index->row);
    window_stack_push(s_compass_window, true);
}

void menu_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d long clicked!", cell_index->row);
    s_number_of_stations = 0;
    menu_layer_reload_data(s_menu_layer);
    send_uid(0); // signal reload request
}

void compass_window_load()
{
    Layer *window_layer = window_get_root_layer(s_compass_window);
    GRect bounds = layer_get_bounds(window_layer);
    
    s_station_name_layer = text_layer_create(GRect(0, 0, 144, 48));
    text_layer_set_font(s_station_name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_background_color(s_station_name_layer, GColorClear);
    text_layer_set_text_color(s_station_name_layer, GColorWhite);
    text_layer_set_text_alignment(s_station_name_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_station_name_layer, GTextOverflowModeTrailingEllipsis);
    layer_add_child(window_layer, text_layer_get_layer(s_station_name_layer));
    
    s_compass_bitmap = gbitmap_create_with_resource(RESOURCE_ID_COMPASS_WHITE);
    s_compass_layer = rot_bitmap_layer_create(s_compass_bitmap);
    bitmap_layer_set_bitmap((BitmapLayer*)s_compass_layer, s_compass_bitmap);
    GRect compass_frame = layer_get_frame((Layer*)s_compass_layer);
    compass_frame.origin.x = (bounds.size.w-compass_frame.size.w)/2;
    compass_frame.origin.y = 62;
    layer_set_frame((Layer*)s_compass_layer, compass_frame);
    layer_add_child(window_layer, bitmap_layer_get_layer((BitmapLayer*)s_compass_layer));
    
    s_distance_layer = text_layer_create(GRect(0, bounds.size.h-24, 144, 24));
    text_layer_set_font(s_distance_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
    text_layer_set_background_color(s_distance_layer, GColorClear);
    text_layer_set_text_color(s_distance_layer, GColorWhite);
    text_layer_set_text_alignment(s_distance_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_distance_layer));
}

void compass_window_unload()
{
    text_layer_destroy(s_distance_layer);
    rot_bitmap_layer_destroy(s_compass_layer);
    gbitmap_destroy(s_compass_bitmap);
    text_layer_destroy(s_station_name_layer);
}

void compass_handler(CompassHeadingData heading)
{
    update_compass_direction(heading.true_heading);
}

void compass_window_appear()
{
    s_selected_station = &s_stations[menu_layer_get_selected_index(s_menu_layer).row];
    text_layer_set_text(s_station_name_layer, s_selected_station->name);
    update_compass_distance();
    send_uid(s_selected_station->uid); // signal selection
    compass_service_subscribe(compass_handler);
}

void compass_window_disappear()
{
    compass_service_unsubscribe();
    send_uid(0); // signal deselection
    s_selected_station = NULL;
}

void init(void)
{
	s_main_window = window_create();
	window_stack_push(s_main_window, true);
    
    Layer *window_layer = window_get_root_layer(s_main_window);

    // Create and add to layer hierarchy
    s_menu_callbacks.get_num_rows = menu_get_num_rows;
    s_menu_callbacks.draw_row = menu_draw_row;
    s_menu_callbacks.select_click = menu_select_click;
    s_menu_callbacks.select_long_click = menu_select_long_click;
    
    GRect bounds = layer_get_bounds(window_layer);
    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_callbacks);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
    menu_layer_set_click_config_onto_window(s_menu_layer, s_main_window);
	
	// Register AppMessage handlers
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);
		
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
    
    s_compass_window = window_create();
    window_set_background_color(s_compass_window, GColorBlack);
    window_set_window_handlers(s_compass_window, (WindowHandlers) {
        .load = compass_window_load,
        .appear = compass_window_appear,
        .disappear = compass_window_disappear,
        .unload = compass_window_unload
    });
}

void deinit(void)
{
    window_destroy(s_compass_window);
	app_message_deregister_callbacks();
    menu_layer_destroy(s_menu_layer);
	window_destroy(s_main_window);
    free(s_stations);
}

int main(void)
{
	init();
	app_event_loop();
	deinit();
}
