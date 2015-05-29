#pragma once

#ifdef PBL_COLOR
#define COLOR(...) __VA_ARGS__
#define BW(...)
#else
#define COLOR(...)
#define BW(...) __VA_ARGS__
#endif

#include "station_menu.h"
#include "compass_window.h"
#include "js_comm.h"
#include "utils.h"
    
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
enum { MAX_STATION_NAME_LENGTH = 32 };

typedef struct Pending
{
    int stations;
    bool location;
    bool bikes;
} Pending;
extern Pending s_pending;

// stations
typedef struct Coordinates
{
    int16_t x, y;
} Coordinates;
extern Coordinates s_last_known_coords;

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
extern int s_stations_size;
extern Station *s_stations;
extern Station **s_sorted_stations;
extern Station *s_selected_station; // pointer to selected station

void reallocate_stations(int size);
void update_station(Station *station);
void update_stations();
