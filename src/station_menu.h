#pragma once

#include <pebble.h>

void station_menu__init();
void station_menu__deinit();

void station_menu__refresh_list();
void station_menu__refresh_icons();
MenuIndex station_menu__get_selection();
void station_menu__set_selection(MenuIndex index, bool animate);
void station_menu__signal_error(const char* msg);
