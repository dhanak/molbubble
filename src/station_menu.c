#include <pebble.h>
#include "mol_bubble.h"

enum { ICON_LAYER_HEIGHT = 36 };

static Window *p_window;
static MenuLayer *p_menu_layer;
static PropertyAnimation *p_menu_animation = NULL;

static Layer *p_icon_layer;
enum { ICON_STATIONS, ICON_BIKES, ICON_LOCATION, ICON_DITHER, ICON_COUNT };
static GBitmap *p_icons[ICON_COUNT] = { NULL };
static PropertyAnimation *p_icon_animation = NULL;

#ifdef PBL_COLOR
enum { PALETTE_STATIONS = 0, PALETTE_BIKES = 2, PALETTE_LOCATION = 4, PALETTE_GRAY = 6 };
static GColor p_palette[8];
#endif

static void stop_animations()
{
    stop_property_animation(&p_menu_animation);
    stop_property_animation(&p_icon_animation);
}

static void resize_menu(GRect *frame)
{
    GRect current_frame = layer_get_frame((Layer*)p_menu_layer);
    if (!grect_equal(frame, &current_frame))
    {
        stop_animations();
        GRect icon_frame = GRect(0, 0, 144, frame->origin.y);
        p_menu_animation = property_animation_create_layer_frame((Layer*)p_menu_layer, &current_frame, frame);
        p_icon_animation = property_animation_create_layer_frame(p_icon_layer, NULL, &icon_frame);
        animation_schedule(property_animation_get_animation(p_menu_animation));
        animation_schedule(property_animation_get_animation(p_icon_animation));
    }
}

static GBitmap* get_icon(int icon_id)
{
#ifdef PBL_COLOR
    gbitmap_set_palette(p_icons[icon_id], &p_palette[icon_id*2], false);
#endif
    return p_icons[icon_id];
}

static GBitmap* get_dither_icon(GContext *ctx, int icon_id)
{
#ifdef PBL_COLOR
    gbitmap_set_palette(p_icons[icon_id], &p_palette[PALETTE_GRAY], false);
    return p_icons[icon_id];
#else
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    return p_icons[ICON_DITHER];
#endif
}

static void icon_layer_update(Layer *layer, GContext *ctx)
{
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    GRect rect = GRect(10, 2, 32, 32);
    graphics_draw_bitmap_in_rect(ctx, get_icon(ICON_STATIONS), rect);
    rect.origin.x += 42;
    graphics_draw_bitmap_in_rect(ctx, get_icon(ICON_BIKES), rect);
    rect.origin.x += 42;
    graphics_draw_bitmap_in_rect(ctx, get_icon(ICON_LOCATION), rect);
    
    rect.origin.x = 10;
    if (s_pending.stations)
    {
        rect.size.h = s_stations_size ? 32 * s_pending.stations/s_stations_size : 32;
        graphics_draw_bitmap_in_rect(ctx, get_dither_icon(ctx, ICON_STATIONS), rect);
        rect.size.h = 32;
    }
    rect.origin.x += 42;
    if (s_pending.bikes)
    {
        graphics_draw_bitmap_in_rect(ctx, get_dither_icon(ctx, ICON_BIKES), rect);
    }
    rect.origin.x += 42;
    if (s_pending.location)
    {
        graphics_draw_bitmap_in_rect(ctx, get_dither_icon(ctx, ICON_LOCATION), rect);
    }
}

static uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *callback_context)
{
    return s_stations_size;
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context)
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

static void menu_selection_changed(MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context)
{
    s_selected_station = s_sorted_stations[new_index.row];
}

static void menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    if (!s_pending.stations && !s_pending.location)
    {
        s_selected_station = s_sorted_stations[cell_index->row];  // just in case no row is selected yet
        compass_window__show();
    }
}

static void menu_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context)
{
    //js_comm__send_request();
}

static MenuLayerCallbacks s_menu_callbacks =
{	// callbacks must be stack allocated
    .get_num_rows = menu_get_num_rows,
    .draw_row = menu_draw_row,
    .selection_changed = menu_selection_changed,
    .select_click = menu_select_click,
    .select_long_click = menu_select_long_click
};

////////////////   E X P O R T E D   F U N C T I O N S   ////////////////

void station_menu__init()
{
#ifdef PBL_COLOR
    for (int i = 0; i < 8; ++i)
    {
        switch (i)
        {
        case PALETTE_STATIONS: p_palette[i] = GColorDarkCandyAppleRed;  break;
        case PALETTE_BIKES:    p_palette[i] = GColorDarkGreen; break;
        case PALETTE_LOCATION: p_palette[i] = GColorArmyGreen; break;
        case PALETTE_GRAY:     p_palette[i] = GColorLightGray; break;
        default: p_palette[i] = GColorWhite;
        }
    }
#endif
    
    p_icons[ICON_STATIONS] = gbitmap_create_with_resource(RESOURCE_ID_MAP);
    p_icons[ICON_BIKES]    = gbitmap_create_with_resource(RESOURCE_ID_BIKE);
    p_icons[ICON_LOCATION] = gbitmap_create_with_resource(RESOURCE_ID_LOCATION);
    p_icons[ICON_DITHER]   = gbitmap_create_with_resource(RESOURCE_ID_DITHER);

    p_window = window_create();
    window_stack_push(p_window, true);
    Layer *window_layer = window_get_root_layer(p_window);
    
    p_icon_layer = layer_create(GRect(0, 0, 144, ICON_LAYER_HEIGHT));
    layer_set_update_proc(p_icon_layer, icon_layer_update);
    layer_add_child(window_layer, p_icon_layer);

    GRect bounds = layer_get_bounds(window_layer);
    bounds.origin.y += ICON_LAYER_HEIGHT;
    bounds.size.h -= ICON_LAYER_HEIGHT;
    p_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(p_menu_layer, NULL, s_menu_callbacks);
    layer_add_child(window_layer, menu_layer_get_layer(p_menu_layer));
    menu_layer_set_click_config_onto_window(p_menu_layer, p_window);
#ifdef PBL_COLOR
    menu_layer_set_highlight_colors(p_menu_layer, GColorDarkGreen, GColorWhite);
#endif // PBL_COLOR
}

void station_menu__deinit()
{
    stop_animations();
    menu_layer_destroy(p_menu_layer);
    layer_destroy(p_icon_layer);
    window_destroy(p_window);

    for (int i = 0; i < ICON_COUNT; i++)
    {
        gbitmap_destroy(p_icons[i]);
    }
}

void station_menu__refresh_list()
{
    menu_layer_reload_data(p_menu_layer);

    GRect frame = layer_get_bounds(window_get_root_layer(p_window));
    if (s_pending.stations || s_pending.location || s_pending.bikes)
    {
        frame.size.h -= ICON_LAYER_HEIGHT;
        frame.origin.y += ICON_LAYER_HEIGHT;
    }
    resize_menu(&frame);
}

void station_menu__refresh_icons()
{
    layer_mark_dirty(p_icon_layer);
}

MenuIndex station_menu__get_selection()
{
    return menu_layer_get_selected_index(p_menu_layer);
}

void station_menu__set_selection(MenuIndex index, bool animate)
{
    menu_layer_set_selected_index(p_menu_layer, index, MenuRowAlignCenter, animate);
}
