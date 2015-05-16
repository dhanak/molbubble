#include <pebble.h>
#include "mol_bubble.h"

Pending s_pending = { INT32_MAX, true, true };
Coordinates s_last_known_coords = { 0, 0 };
int s_stations_size = 0;
Station *s_stations = NULL;
Station **s_sorted_stations = NULL;
Station *s_selected_station = NULL; // pointer to selected station

static void swap_stations(int a, int b)
{
    Station* tmp = s_sorted_stations[a];
    s_sorted_stations[a] = s_sorted_stations[b];
    s_sorted_stations[b] = tmp;
}

static void sort_stations(int start, int end)
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

static void persist_write_stations()
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

static void persist_read_stations()
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

////////////////   E X P O R T E D   F U N C T I O N S   ////////////////

void init(void)
{
    persist_read_stations();
    js_comm_init();
    station_menu_init();
    compass_window_init();
}

void deinit(void)
{
    compass_window_deinit();
    station_menu_deinit();
    js_comm_deinit();
    persist_write_stations();
    free(s_stations);
    free(s_sorted_stations);
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
    
        // update selection
        MenuIndex selection = menu_get_selection();
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
            menu_set_selection(selection, true);
        }
    }
}

int main()
{
    init();
    app_event_loop();
    deinit();
}
