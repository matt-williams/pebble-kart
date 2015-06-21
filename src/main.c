#include <pebble.h>

static Window *s_window;
static Layer *s_window_layer;
static GRect s_window_bounds;
static uint8_t *s_map;
static GBitmap *s_tiles;
static uint8_t *s_tile_data;
static GBitmap *s_font;
static uint8_t *s_font_data;
static AppTimer *s_timer;

typedef struct TimeMs {
  time_t time_s;
  uint16_t time_ms;
} TimeMs;
TimeMs s_start_time;

void time_ms_get_time(TimeMs* t) {
  time_ms(&t->time_s, &t->time_ms);
}

uint32_t time_ms_get_time_since(TimeMs* t) {
  TimeMs t2;
  time_ms_get_time(&t2);
  return (t2.time_s - t->time_s) * 1000 + ((t2.time_ms < t->time_ms) ? 1000 : 0) + (t2.time_ms - t->time_ms);
}

inline static int get_tile(int32_t x, int32_t z)
{
  int32_t tile_x = x / 0x100000;
  int32_t tile_z = z / 0x100000;
  tile_x = (tile_x < 0) ? 0 : (tile_x >= 64) ? 63 : tile_x;
  tile_z = (tile_z < 0) ? 0 : (tile_z >= 64) ? 63 : tile_z;
  return s_map[tile_x + tile_z * 64];
}

inline static int get_bit_2d(int32_t x, int32_t z)
{
  int tile = get_tile(x, z);
  int32_t pixel_x = (tile % 8) * 16 + (((uint32_t)x) / 0x10000) % 16;
  int32_t pixel_z = (tile / 8) * 16 + (((uint32_t)z) / 0x10000) % 16;
  return (s_tile_data[(pixel_x / 8) + pixel_z * 16] >> (pixel_x % 8)) & 1;
}

typedef struct Kart {
  int32_t x;
  int32_t z;
  int16_t vx;
  int16_t vz;
  uint16_t r;
  uint16_t dr;
  int32_t sin_r;
  int32_t cos_r;
} Kart;
Kart *s_kart;

Kart *kart_create() {
  Kart *kart = malloc(sizeof(Kart));
  kart->x = 0x500000;
  kart->z = 0x3980000;
  kart->vx = 0;
  kart->vz = 0;
  kart->r = 0;
  kart->sin_r = sin_lookup(kart->r);
  kart->cos_r = cos_lookup(kart->r);
  return kart;
}

void kart_steer(Kart* kart, int r, int dr) {
  kart->dr = dr;
  kart->r += r;
  kart->sin_r = sin_lookup(kart->r);
  kart->cos_r = cos_lookup(kart->r);
}

#define KART_ACCELERATION 0x8000
#define KART_DRAG 1.0

void kart_update(Kart* kart)
{
  kart_steer(kart, kart->dr, kart->dr);
  int32_t ax = kart->sin_r * KART_ACCELERATION / TRIG_MAX_RATIO;
  int32_t az = -kart->cos_r * KART_ACCELERATION / TRIG_MAX_RATIO;
  kart->vx = kart->vx * (1.0 - KART_DRAG) + ax;
  kart->vz = kart->vz * (1.0 - KART_DRAG) + az;
  int32_t new_x = kart->x + kart->vx * 0x0a;
  int32_t new_z = kart->z + kart->vz * 0x0a;
  if (get_tile(new_x, new_z) != 4) {
    kart->x = new_x;
    kart->z = new_z;
  }
}


void draw_timer(uint8_t* raw, uint32_t time_ms, bool flash)
{
  int mins = time_ms / 60000;
  int ten_secs = (time_ms % 60000) / 10000;
  int secs = (time_ms % 10000) / 1000;
  int decisecs = (time_ms % 1000) / 100;
  int centisecs = (time_ms % 100) / 10;
  int millisecs = (time_ms % 10);
  for (int y = 0; y < 16; y++)
  {
    if (mins > 0) raw[5 + y * 20] = s_font_data[mins + y * 12];
    if (flash && (mins > 0) && (decisecs < 5)) raw[6 + y * 20] = s_font_data[10 + y * 12];
    if (ten_secs > 0) raw[7 + y * 20] = s_font_data[ten_secs + y * 12];
    raw[8 + y * 20] = s_font_data[secs + y * 12];
    if (flash && (decisecs < 5)) raw[9 + y * 20] = s_font_data[10 + y * 12];
    raw[10 + y * 20] = s_font_data[decisecs + y * 12];
    raw[11 + y * 20] = s_font_data[centisecs + y * 12];
    raw[12 + y * 20] = s_font_data[millisecs + y * 12];
  }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  kart_steer(s_kart, 0, -500);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  kart_steer(s_kart, 0, 500);
}

static void release_handler(ClickRecognizerRef recognizer, void *context) {
  kart_steer(s_kart, 0, 0);
}

static void click_config_provider(void *context) {
  window_raw_click_subscribe(BUTTON_ID_UP, up_click_handler, release_handler, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, down_click_handler, release_handler, NULL);
}

#define TWICE_COS_HALF_FOV_X (0x10000 / 16)
#define TWICE_COS_HALF_FOV_Y (0x10000 / 8)
#define DISPLAY_WIDTH 144
#define DISPLAY_HEIGHT 168
#define CAM_Y 0x10000

inline static int get_bit_3d(int32_t x, int32_t y)
{
  if (y < (DISPLAY_HEIGHT / 2)) return 0;
  int32_t rx = (x - DISPLAY_WIDTH / 2) * TWICE_COS_HALF_FOV_X / DISPLAY_WIDTH;
  int32_t ry = (y - DISPLAY_HEIGHT / 2) * TWICE_COS_HALF_FOV_Y / DISPLAY_HEIGHT;
  int32_t ray_x = rx * s_kart->cos_r / 0x1000 + s_kart->sin_r;
  int32_t ray_y = ry;
  int32_t ray_z = rx * s_kart->sin_r / 0x1000 - s_kart->cos_r;
  int32_t d = CAM_Y / ray_y;
  return get_bit_2d(ray_x * d + s_kart->x, ray_z * d + s_kart->z);
}

static void update_proc(Layer *this_layer, GContext *context) {
  GBitmap* display = (GBitmap*)context;
  uint32_t* raw = (uint32_t*)gbitmap_get_data(display);
  GRect bounds = gbitmap_get_bounds(display);
  int row_size_words = (bounds.size.w + 31) / 32;

  for (int y = 0; y < bounds.size.h; y++) {
    for (int x = 0; x < bounds.size.w;) {
      uint32_t out = 0;
      for (int bit = 0; bit < 32; x++, bit++) {
        out |= (get_bit_3d(x, y) << bit);
      }
      raw[x / 32 - 1 + y * row_size_words] = out;
    }
  }

  draw_timer((uint8_t*)raw, time_ms_get_time_since(&s_start_time), true);
}

static void window_load(Window *window) {
  Layer* window_layer = window_get_root_layer(window);
  s_window_bounds = layer_get_bounds(window_layer);
  GRect window_frame = layer_get_frame(window_layer);

  s_window_layer = layer_create(window_frame);
  layer_set_update_proc(s_window_layer, update_proc);
  layer_add_child(window_layer, s_window_layer);
}

static void window_unload(Window *window) {
}

static void core_loop(void *data) {
  AccelData accel_data = {0,};
  accel_service_peek(&accel_data);

  kart_steer(s_kart, accel_data.x, s_kart->dr);
  kart_update(s_kart);

  layer_mark_dirty(s_window_layer);
  s_timer = app_timer_register(20, core_loop, NULL);
}

void load_map(uint32_t resource_id) {
  ResHandle handle = resource_get_handle(resource_id);
  s_map = (uint8_t*)malloc(4096);
  resource_load(handle, s_map, 4096);
}


static void start_race(uint32_t resource_id) {
  light_enable(true);

  load_map(resource_id);
  s_tiles = gbitmap_create_with_resource(RESOURCE_ID_TILES);
  s_tile_data = gbitmap_get_data(s_tiles);
  s_font = gbitmap_create_with_resource(RESOURCE_ID_FONT);
  s_font_data = gbitmap_get_data(s_font);

  time_ms_get_time(&s_start_time);

  s_kart = kart_create();

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, false);

  accel_data_service_subscribe(0, NULL);
  s_timer = app_timer_register(50, core_loop, NULL);
}


static void end_race(void) {
  app_timer_cancel(s_timer);
  accel_data_service_unsubscribe();

  window_destroy(s_window);

  light_enable(false);
}





static void init(void) {
  start_race(RESOURCE_ID_TRACK1);
}

static void deinit(void) {
  end_race();
}

int main(void) {
  init();

  app_event_loop();

  deinit();
}
