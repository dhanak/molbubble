#include <pebble.h>
#include "mol_bubble.h"

static void copy_name(char *dst, const char *src)
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

static void inbox_received_callback(DictionaryIterator *iterator, void *context)
{
    Tuple *t;
    
    if ((t = dict_find(iterator, KEY_NUM_STATIONS)) != NULL)
    {   // station publish/update begins, allocate vector (if necesary)
        reallocate_stations(t->value->int32);
        station_menu__refresh_list();
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
                station_menu__refresh_icons();
            }
            if (i == s_stations_size-1)
            {   // received last refresh, update
                update_stations();
            }
            // update display
            station_menu__refresh_list();
            if (station == s_selected_station)
            {
                update_station(station);
                compass_window__update_distance();
            }
        }
    }
    else if ((t = dict_find(iterator, KEY_UPDATE)) != NULL)
    {   // station update package
        for (int start = t->value->data[0], i = 1; i < t->length && start+i-1 < s_stations_size; i++)
        {
            s_stations[start+i-1].bikes = t->value->data[i];
        }
        if (s_pending.bikes)
        {
            s_pending.bikes = false;
            station_menu__refresh_icons();
        }
        // update display
        station_menu__refresh_list();
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
        if (s_pending.location)
        {
            s_pending.location = false;
            station_menu__refresh_icons();
        }
        update_stations();
        // update display
        station_menu__refresh_list();
        compass_window__update_distance();
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context)
{
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context)
{
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context)
{
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

////////////////   E X P O R T E D   F U N C T I O N S   ////////////////

void js_comm__init()
{
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);

    app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

void js_comm__deinit()
{
    app_message_deregister_callbacks();
}

void js_comm__send_request()
{
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_end(iter);
    app_message_outbox_send();
}
