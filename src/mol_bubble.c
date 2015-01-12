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
    MAX_STATION_NAME_LENGTH = 64,
    MAX_DISTANCE_LENGTH     = 32,
};

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
Station *s_selected_station = NULL; // pointer to selected station (if any)

// main window UI elements
Window *s_main_window;
MenuLayer *s_menu_layer;
MenuLayerCallbacks s_menu_callbacks;

// compass window UI elements
Window *s_compass_window;
GBitmap* s_compass_bitmap;
RotBitmapLayer *s_compass_layer;
TextLayer *s_station_name_layer;
TextLayer *s_distance_layer;
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
        return;
    
    free(s_stations);
    free(s_sorted_stations);
    s_stations_size = size;
    s_stations = calloc(s_stations_size, sizeof(Station));
    s_sorted_stations = calloc(s_stations_size, sizeof(Station*));
    for (int i = 0; i < size; i++)
    {
        s_sorted_stations[i] = &s_stations[i];
    }
}

void update_station(Station *station)
{
    int32_t dx = station->coords.x - s_last_known_coords.x;
    int32_t dy = station->coords.y - s_last_known_coords.y;
    station->distance = sqrt32(dx*dx + dy*dy);
    station->bearing = atan2_lookup(dx, -dy);
}

void swap_stations(int a, int b)
{
    Station* tmp = s_sorted_stations[a];
    s_sorted_stations[a] = s_sorted_stations[b];
    s_sorted_stations[b] = tmp;
}

void sort_stations(int start, int end)
{
    if (end > start)
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

void persist_write_stations()
{
    persist_write_int(0, s_stations_size);
    for (int i = 0; i < s_stations_size; i++)
    {
        persist_write_data(i+1, &s_stations[i], STATION_PERSIST_SIZE);
    }
}

void persist_read_stations()
{
    int size = persist_read_int(0);
    reallocate_stations(size);
    for (int i = 0; i < size; i++)
    {
        persist_read_data(i+1, &s_stations[i], STATION_PERSIST_SIZE);
    }
}

// compass window functions

void update_compass_distance()
{
    if (s_selected_station)
    {
        snprintf(s_distance_str, MAX_DISTANCE_LENGTH, "%d meters", s_selected_station->distance);
        text_layer_set_text(s_distance_layer, s_distance_str);
    }
}

void update_compass_direction(CompassHeading heading)
{
    if (s_selected_station)
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
        menu_layer_reload_data(s_menu_layer);
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
                    strncpy(station->name, t->value->cstring, MAX_STATION_NAME_LENGTH);
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
            update_station(station);
            sort_stations(0, s_stations_size-1);
            // update display
            menu_layer_reload_data(s_menu_layer);
            if (station == s_selected_station)
            {
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
        // update display
        menu_layer_reload_data(s_menu_layer);
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
        for (int i = 0; i < s_stations_size; i++)
        {
            update_station(&s_stations[i]);
        }
        sort_stations(0, s_stations_size-1);
        // update display
        menu_layer_reload_data(s_menu_layer);
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

uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *callback_context)
{
    return s_stations_size == 0 ? 1 : s_stations_size;
}

void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context)
{
    if (s_stations_size == 0)
    {
        menu_cell_basic_draw(ctx, cell_layer, "Loading...", NULL, NULL);
    }
    else
    {
        Station *station = s_sorted_stations[cell_index->row];
        char buf[64] = { '.', '.', '.', 0 };
        if (station->name[0] && station->bikes)
        {   // row details already loaded
            snprintf(buf, 64, "%dm, %d bikes/%d racks", station->distance, station->bikes, station->racks);
        }
        menu_cell_basic_draw(ctx, cell_layer, station->name[0] ? station->name : "...", buf, NULL);
    }
}

void menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d clicked!", cell_index->row);
    window_stack_push(s_compass_window, true);
}

void menu_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d long clicked!", cell_index->row);
    send_request();
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
    s_selected_station = s_sorted_stations[menu_layer_get_selected_index(s_menu_layer).row];
    text_layer_set_text(s_station_name_layer, s_selected_station->name);
    update_compass_distance();
    compass_service_subscribe(compass_handler);
}

void compass_window_disappear()
{
    compass_service_unsubscribe();
    s_selected_station = NULL;
}

void init(void)
{
    // read persistence data
    persist_read_stations();
    
    // create main window
	s_main_window = window_create();
	window_stack_push(s_main_window, true);
    
    Layer *window_layer = window_get_root_layer(s_main_window);

    s_menu_callbacks.get_num_rows = menu_get_num_rows;
    s_menu_callbacks.draw_row = menu_draw_row;
    s_menu_callbacks.select_click = menu_select_click;
    s_menu_callbacks.select_long_click = menu_select_long_click;
    
    GRect bounds = layer_get_bounds(window_layer);
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

	// register AppMessage handlers
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);
		
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

void deinit(void)
{
    window_destroy(s_compass_window);
	app_message_deregister_callbacks();
    menu_layer_destroy(s_menu_layer);
	window_destroy(s_main_window);
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
