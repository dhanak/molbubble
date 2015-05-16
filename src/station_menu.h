#pragma once

void station_menu_init();
void station_menu_deinit();

void refresh_menu();
void refresh_icons();
MenuIndex menu_get_selection();
void menu_set_selection(MenuIndex index, bool animate);
