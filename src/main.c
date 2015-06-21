#include <pebble.h>
#include "race.h"

#define NUM_MENU_SECTIONS 1
#define NUM_TRACKS 2

static Window *s_main_window;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[NUM_MENU_SECTIONS];
static SimpleMenuItem s_menu_items[NUM_TRACKS];

typedef struct Track {
  char *name;
  uint32_t track_resource_id;
  uint32_t thumbnail_resource_id;
} Track;

static Track s_tracks[NUM_TRACKS] = {
  {"Loop", RESOURCE_ID_TRACK1, RESOURCE_ID_TRACK1_THUMBNAIL},
  {"Twisty", RESOURCE_ID_TRACK2, RESOURCE_ID_TRACK2_THUMBNAIL}
};

static void menu_select_callback(int index, void *context) {
  start_race(s_tracks[index].track_resource_id);
}

static void main_window_load(Window *window) {
  for (int index = 0; index < NUM_TRACKS; index++)
  {
    s_menu_items[index] = (SimpleMenuItem) {
      .title = s_tracks[index].name,
      .callback = menu_select_callback,
      .icon = gbitmap_create_with_resource(s_tracks[index].thumbnail_resource_id),
    };
  }

  s_menu_sections[0] = (SimpleMenuSection) {
    .title = "Kart",
    .num_items = NUM_TRACKS,
    .items = s_menu_items,
  };

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  s_simple_menu_layer = simple_menu_layer_create(bounds, window, s_menu_sections, NUM_MENU_SECTIONS, NULL);

  layer_add_child(window_layer, simple_menu_layer_get_layer(s_simple_menu_layer));
}

void main_window_unload(Window *window) {
  simple_menu_layer_destroy(s_simple_menu_layer);
  for (int index = 0; index < NUM_TRACKS; index++)
  {
    gbitmap_destroy(s_menu_items[index].icon);
  }
}

static void init(void) {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  end_race();
  window_destroy(s_main_window);
}

int main(void) {
  init();

  app_event_loop();

  deinit();
}
