#include <pebble.h>

Window *s_window;
MenuLayer *s_menu_layer;
MenuLayerCallbacks s_menu_callbacks;

enum
{
    MAX_STATION_NAME_LENGTH = 32,
    MAX_NUMBER_OF_STATIONS = 10,
};

struct Station
{
    char name[MAX_STATION_NAME_LENGTH];
    uint16_t distance;
    uint8_t bikes;
    uint8_t racks;
} s_stations[MAX_NUMBER_OF_STATIONS];
uint s_number_of_stations = 0;

// Key values for AppMessage Dictionary
enum
{
    KEY_NAME,
    KEY_DISTANCE,
    KEY_BIKES,
    KEY_RACKS
};

// Write message to buffer & send
void send_message(void)
{
	DictionaryIterator *iter;
	
	app_message_outbox_begin(&iter);
	//dict_write_uint8(iter, STATUS, 0x1);
	
	dict_write_end(iter);
  	app_message_outbox_send();
}

// Called when a message is received from PebbleKitJS
void inbox_received_callback(DictionaryIterator *iterator, void *context)
{
    // Get the first pair
    Tuple *t = dict_read_first(iterator);
    s_number_of_stations = 0;

    // Process all pairs present
    while(t != NULL)
    {
        // Process this pair's key
        struct Station *station = &s_stations[s_number_of_stations];
        switch (t->key)
        {
        case KEY_NAME:
            strncpy(station->name, t->value->cstring, MAX_STATION_NAME_LENGTH);
            break;
        case KEY_DISTANCE:
            station->distance = t->value->int32;
            break;
        case KEY_BIKES:
            station->bikes = t->value->int32;
        case KEY_RACKS:
            APP_LOG(APP_LOG_LEVEL_INFO, "Key %d received with value %d", (int)t->key, (int)t->value->int32);
            station->racks = t->value->int32;
            break;
        }

        // Get next pair, if any
        t = dict_read_next(iterator);
        s_number_of_stations++;
    }
    
    // Reload menu
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
    return s_number_of_stations;
}

void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context)
{
    struct Station *station = &s_stations[cell_index->row];
    char subtext[64];
    snprintf(subtext, 64, "%d meters, %d bikes/%d racks", station->distance, station->bikes, station->racks);
    menu_cell_basic_draw(ctx, cell_layer, station->name, subtext, NULL);
}

void menu_select_click(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d clicked!", cell_index->row);
}

void menu_select_long_click(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Item %d long clicked!", cell_index->row);
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
    
    s_menu_layer = menu_layer_create(GRect(0, 0, 144, 168));
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
}

int main(void)
{
	init();
	app_event_loop();
	deinit();
}
