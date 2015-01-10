#include <pebble.h>

Window *s_window;
MenuLayer *s_menu_layer;
MenuLayerCallbacks s_menu_callbacks;

enum
{
    MAX_STATION_NAME_LENGTH = 64
};

struct Station
{
    char name[MAX_STATION_NAME_LENGTH];
    uint16_t distance;
    int16_t heading;
    uint8_t bikes;
    uint8_t racks;
} *s_stations = NULL;
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
};

// Called when a message is received from PebbleKitJS
void inbox_received_callback(DictionaryIterator *iterator, void *context)
{
    Tuple *t = dict_read_first(iterator);
    while(t != NULL)
    {
        struct Station *station = s_stations ? &s_stations[s_number_of_stations] : NULL;
        switch (t->key)
        {
        case KEY_NUM_STATIONS:
            free(s_stations);
            s_number_of_stations = -1;
            s_stations_length = t->value->int32;
            s_stations = calloc(s_stations_length, sizeof(struct Station));
            break;
        case KEY_NAME:
            if (station) strncpy(station->name, t->value->cstring, MAX_STATION_NAME_LENGTH);
            break;
        case KEY_DISTANCE:
            if (station) station->distance = t->value->int32;
            break;
        case KEY_BIKES:
            if (station) station->bikes = t->value->int32;
            break;
        case KEY_RACKS:
            if (station) station->racks = t->value->int32;
            break;
        case KEY_HEADING:
            if (station) station->heading = t->value->int32;
            break;
        }
        t = dict_read_next(iterator);
    }
    s_number_of_stations++;
    menu_layer_reload_data(s_menu_layer);
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

uint16_t menu_get_num_rows(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context)
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
        struct Station *station = &s_stations[cell_index->row];
        snprintf(buf, 64, "%dm, %d bikes/%d racks", station->distance, station->bikes, station->racks);
        menu_cell_basic_draw(ctx, cell_layer, station->name, buf, NULL);
    }
    else
    {   // row data still loading
        menu_cell_basic_draw(ctx, cell_layer, "...", NULL, NULL);
    }
}

void menu_select_click(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d clicked!", cell_index->row);
}

void menu_select_long_click(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d long clicked!", cell_index->row);

    DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	dict_write_end(iter);
  	app_message_outbox_send();
    
    s_number_of_stations = 0;
    menu_layer_reload_data(s_menu_layer);
}

void init(void)
{
	s_window = window_create();
	window_stack_push(s_window, true);
    
    Layer *window_layer = window_get_root_layer(s_window);

    // Create and Add to layer hierarchy
    s_menu_callbacks.get_num_rows = menu_get_num_rows;
    s_menu_callbacks.draw_row = menu_draw_row;
    s_menu_callbacks.select_click = menu_select_click;
    s_menu_callbacks.select_long_click = menu_select_long_click;
    
    GRect bounds = layer_get_bounds(window_layer);
    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_callbacks);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
    menu_layer_set_click_config_onto_window(s_menu_layer, s_window);
	
	// Register AppMessage handlers
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);
		
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

void deinit(void)
{
	app_message_deregister_callbacks();
    menu_layer_destroy(s_menu_layer);
	window_destroy(s_window);
    free(s_stations);
}

int main(void)
{
	init();
	app_event_loop();
	deinit();
}
