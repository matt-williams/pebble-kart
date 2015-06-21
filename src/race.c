#include <pebble.h>

static Window *s_window;
static Layer *s_window_layer;
static GRect s_window_bounds;
static uint8_t *s_map;
static GBitmap *s_kart_black;
static GBitmap *s_kart_white;
static GBitmap *s_kart_black2;
static GBitmap *s_kart_white2;
static GBitmap *s_kart_black3;
static GBitmap *s_kart_white3;
static GBitmap *s_tiles;
static uint8_t *s_tile_data;
static GBitmap *s_sky;
static GBitmap *s_font;
static uint8_t *s_font_data;
static AppTimer *s_timer;

#define TWICE_COS_HALF_FOV_X (0x10000 / 16)
#define TWICE_COS_HALF_FOV_Y (0x10000 / 8)
#define DISPLAY_WIDTH 144
#define DISPLAY_HEIGHT 152
#define CAM_Y 0x10000
#define NUM_OTHER_KARTS 3

static uint32_t s_map_resource_id;
static int s_pos;
static bool s_completed;
typedef struct TimeMs {
  time_t time_s;
  uint16_t time_ms;
} TimeMs;
TimeMs s_start_time;
uint32_t s_last_time_ms;

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

inline static bool is_tile_x_finish_line(int tile) {
  return ((tile == 10) || (tile == 34) || (tile == 40) || (tile == 48));
}

inline static bool is_tile_z_finish_line(int tile) {
  return ((tile == 24) || (tile == 27) || (tile == 41) || (tile == 49));
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
  uint16_t laps;
} Kart;
Kart *s_kart;
Kart *s_other_karts[NUM_OTHER_KARTS];

Kart *kart_create(int index) {
  Kart *kart = malloc(sizeof(Kart));
  kart->x = 0x500000;
  kart->z = 0x3980000 - index * 0x100000;
  kart->vx = 0;
  kart->vz = 0;
  kart->r = 0;
  kart->sin_r = sin_lookup(kart->r);
  kart->cos_r = cos_lookup(kart->r);
  kart->laps = 0;
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
  int old_tile = get_tile(kart->x, kart->z);
  int new_tile = get_tile(new_x, new_z);
  if (new_tile != 4) {
    kart->x = new_x;
    kart->z = new_z;
  }

  if (!is_tile_x_finish_line(old_tile) &&
      !is_tile_z_finish_line(old_tile))
  {
    if ((is_tile_x_finish_line(new_tile)) &&
        (kart->vx > 0)) {
      kart->laps++;
    } else if ((is_tile_z_finish_line(new_tile)) &&
               (kart->vz < 0)) {
      kart->laps++;
    }
  } else if (!is_tile_x_finish_line(new_tile) &&
             !is_tile_z_finish_line(new_tile))
  {
    if ((is_tile_x_finish_line(old_tile)) &&
        (kart->vx < 0)) {
      kart->laps--;
    } else if ((is_tile_z_finish_line(old_tile)) &&
               (kart->vz > 0)) {
      kart->laps--;
    }
  }
}


void kart_draw(GContext* context, Kart* kart, int32_t view_x, int32_t view_z, uint16_t view_r) {
  uint16_t dr = ((view_r - kart->r + 0x8000) % 0x10000) / 0x2000;
  int32_t dx = kart->x - view_x;
  int32_t dz = kart->z - view_z;
  int32_t projected_dx = dx / 0x100 * cos_lookup(view_r) / 0x100 + dz / 0x100 * sin_lookup(view_r) / 0x100;
  int32_t projected_dz = dx / 0x100 * sin_lookup(view_r) / 0x100 - dz / 0x100 * cos_lookup(view_r) / 0x100;

  if (projected_dz > 0)
  {
    int32_t screen_x = projected_dx / (projected_dz / 0x10000) * DISPLAY_WIDTH / TWICE_COS_HALF_FOV_X + DISPLAY_WIDTH / 2;
    int32_t screen_y = CAM_Y / (projected_dz / 0x10000) * DISPLAY_HEIGHT / TWICE_COS_HALF_FOV_Y + DISPLAY_HEIGHT / 2;

    //app_log(APP_LOG_LEVEL_ERROR, "", 1, "CAM_Y = %ld, projected_dz = %ld, DISPLAY_HEIGHT = %ld, TWICE_COS_HALF_FOV_Y = %ld", (uint32_t)CAM_Y, projected_dz, (uint32_t)DISPLAY_HEIGHT, (uint32_t)TWICE_COS_HALF_FOV_Y);
    //app_log(APP_LOG_LEVEL_ERROR, "", 1, "CAM_Y / projected_dz = %ld, ... * DISPLAY_HEIGHT = %ld, ... / TWICE_COS_HALF_FOV_Y = %ld", CAM_Y / projected_dz, CAM_Y / projected_dz * DISPLAY_HEIGHT, CAM_Y / projected_dz * DISPLAY_HEIGHT / TWICE_COS_HALF_FOV_Y);
    //if (kart == s_other_karts[0]) {
    //    app_log(APP_LOG_LEVEL_ERROR, "", 1, "view_x = %ld, view_z = %ld, kart->x = %ld, kart->z = %ld", view_x, view_z, kart->x, kart->z);
    //    app_log(APP_LOG_LEVEL_ERROR, "", 1, "dx = %ld, dz = %ld, projected_dx = %ld, projected_dz = %ld", dx, dz, projected_dx, projected_dz);
    //    app_log(APP_LOG_LEVEL_ERROR, "", 1, "sin(view_r) = %ld, cos(view_r) = %ld", sin_lookup(view_r), cos_lookup(view_r));
    //}
    //app_log(APP_LOG_LEVEL_ERROR, "", 1, "screen_x = %ld, screen_y = %ld", screen_x, screen_y);
    
    GBitmap* kart_black;
    GBitmap* kart_white;
    GRect src_rect;
    GRect dst_rect;
    if (projected_dz < 0x400000) {
      kart_black = s_kart_black;
      kart_white = s_kart_white;
      src_rect = (GRect){.origin = {.x = dr * 64, .y = 0}, .size = {.w = 64, .h = 42}};
      dst_rect = (GRect){.origin = {.x = screen_x - 32, .y = screen_y - 47}, .size = {.w = 64, .h = 42}};
    } else if (projected_dz < 0x800000) {
      kart_black = s_kart_black2;
      kart_white = s_kart_white2;
      src_rect = (GRect){.origin = {.x = dr * 32, .y = 0}, .size = {.w = 32, .h = 21}};
      dst_rect = (GRect){.origin = {.x = screen_x - 16, .y = screen_y - 28}, .size = {.w = 32, .h = 21}};
    } else {
      kart_black = s_kart_black3;
      kart_white = s_kart_white3;
      src_rect = (GRect){.origin = {.x = dr * 16, .y = 0}, .size = {.w = 16, .h = 11}};
      dst_rect = (GRect){.origin = {.x = screen_x - 8, .y = screen_y - 16}, .size = {.w = 16, .h = 11}};
    }

    graphics_context_set_compositing_mode(context, GCompOpClear);
    GBitmap* kart_section = gbitmap_create_as_sub_bitmap(kart_black, src_rect);
    graphics_draw_bitmap_in_rect(context, kart_section, dst_rect);
    gbitmap_destroy(kart_section);

    graphics_context_set_compositing_mode(context, GCompOpOr);
    kart_section = gbitmap_create_as_sub_bitmap(kart_white, src_rect);
    graphics_draw_bitmap_in_rect(context, kart_section, dst_rect);
    gbitmap_destroy(kart_section);
  }
}

void kart_destroy(Kart* kart) {
  free(kart);
}

static void draw_status(uint8_t* raw, uint32_t time_ms, int lap, int pos, bool flash)
{
  int mins = time_ms / 60000;
  int ten_secs = (time_ms % 60000) / 10000;
  int secs = (time_ms % 10000) / 1000;
  int decisecs = (time_ms % 1000) / 100;
  int centisecs = (time_ms % 100) / 10;
  int millisecs = (time_ms % 10);

  uint8_t glyphs[18];
  glyphs[0] = lap;
  glyphs[1] = 11;
  glyphs[2] = 3;
  glyphs[3] = 12;
  glyphs[4] = 15; 
  glyphs[5] = (mins > 0) ? mins : 15;
  glyphs[6] = (flash && (mins > 0) && (decisecs < 5)) ? 10 : 15;
  glyphs[7] = ((mins > 0) || (ten_secs > 0)) ? ten_secs : 15;
  glyphs[8] = secs;
  glyphs[9] = (flash && (decisecs < 5)) ? 10 : 15;
  glyphs[10] = decisecs;
  glyphs[11] = centisecs;
  glyphs[12] = millisecs;
  glyphs[13] = 15;
  glyphs[14] = 13;
  glyphs[15] = pos;
  glyphs[16] = 11;
  glyphs[17] = 4;

  for (int y = 0; y < 16; y++)
  {
    for (int x = 0; x < 18; x++)
    {
      raw[x + y * 20] = s_font_data[glyphs[x] + y * 16];
    }
  }
  for (int y = 16; y < 28; y++)
  {
    for (int x = 0; x < 18; x++)
    {
      raw[x + y * 20] = 0;
    }
  }
}


static void draw_sky(GContext* context, int32_t r)
{
  int32_t r_pixels = r * DISPLAY_WIDTH / 0x4000;

  GRect rect = {.origin = {.x = r_pixels, .y = 0}, .size = {.w = (r_pixels + DISPLAY_WIDTH > 288) ? 288 - r_pixels : DISPLAY_WIDTH, .h = 48}};
  GBitmap* sky_section = gbitmap_create_as_sub_bitmap(s_sky, rect);
  rect.origin = (GPoint){.x = 0, .y = DISPLAY_HEIGHT/2 - 48};
  graphics_draw_bitmap_in_rect(context, sky_section, rect);
  gbitmap_destroy(sky_section);

  if (rect.size.w != DISPLAY_WIDTH) {
    rect = (GRect){.origin = {.x = 0, .y = 0}, .size = {.w = r_pixels + DISPLAY_WIDTH - 288, .h = 48}};
    sky_section = gbitmap_create_as_sub_bitmap(s_sky, rect);
    rect.origin = (GPoint){.x = 288 - r_pixels, .y = DISPLAY_HEIGHT/2 - 48};
    graphics_draw_bitmap_in_rect(context, sky_section, rect);
    gbitmap_destroy(sky_section);
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

inline static int get_bit_3d(int32_t view_x, int32_t view_z, int32_t x, int32_t y)
{
  int32_t rx = (x - DISPLAY_WIDTH / 2) * TWICE_COS_HALF_FOV_X / DISPLAY_WIDTH;
  int32_t ry = (y - DISPLAY_HEIGHT / 2) * TWICE_COS_HALF_FOV_Y / DISPLAY_HEIGHT;
  int32_t ray_x = rx * s_kart->cos_r / 0x1000 + s_kart->sin_r;
  int32_t ray_y = ry;
  int32_t ray_z = rx * s_kart->sin_r / 0x1000 - s_kart->cos_r;
  int32_t d = CAM_Y / ray_y;
  return get_bit_2d(ray_x * d + view_x, ray_z * d + view_z);
}

static void update_proc(Layer *this_layer, GContext *context) {
  GBitmap* display = graphics_capture_frame_buffer(context);
  uint32_t* raw = (uint32_t*)gbitmap_get_data(display);
  GRect bounds = gbitmap_get_bounds(display);
  int row_size_words = (bounds.size.w + 31) / 32;

  int32_t view_x = s_kart->x - s_kart->sin_r * CAM_Y / (DISPLAY_HEIGHT / 2 * TWICE_COS_HALF_FOV_Y / DISPLAY_HEIGHT);
  int32_t view_z = s_kart->z + s_kart->cos_r * CAM_Y / (DISPLAY_HEIGHT / 2 * TWICE_COS_HALF_FOV_Y / DISPLAY_HEIGHT);

  s_last_time_ms = time_ms_get_time_since(&s_start_time);
  draw_status((uint8_t*)raw, s_last_time_ms, s_kart->laps, s_pos, true);
  for (int y = DISPLAY_HEIGHT / 2; y < bounds.size.h; y++) {
    for (int x = 0; x < bounds.size.w;) {
      uint32_t out = 0;
      for (int bit = 0; bit < 32; x++, bit++) {
        out |= (get_bit_3d(view_x, view_z, x, y) << bit);
      }
      raw[x / 32 - 1 + y * row_size_words] = out;
    }
  }

  graphics_release_frame_buffer(context, display);
  draw_sky(context, s_kart->r);
  kart_draw(context, s_kart, view_x, view_z, s_kart->r);
  for (int ii = 0; ii < NUM_OTHER_KARTS; ii++) {
    //kart_draw(context, s_other_karts[ii], view_x, view_z, s_kart->r);
  }
}

void load_map(uint32_t resource_id) {
  ResHandle handle = resource_get_handle(resource_id);
  s_map = (uint8_t*)malloc(4096);
  resource_load(handle, s_map, 4096);
}

static void core_loop(void *data) {
  if (s_kart->laps < 4)
  {
    AccelData accel_data = {0,};
    accel_service_peek(&accel_data);

    kart_steer(s_kart, accel_data.x, s_kart->dr);
    kart_update(s_kart);
    for (int ii = 0; ii < NUM_OTHER_KARTS; ii++) {
      kart_update(s_other_karts[ii]);
    }

    layer_mark_dirty(s_window_layer);
    s_timer = app_timer_register(20, core_loop, NULL);
  }
  else if (!s_completed)
  {
    s_completed = true;
    int32_t record_ms = persist_read_int(s_map_resource_id);
    if (record_ms == 0) {
      record_ms = 180000;
    }
    if (record_ms > (int32_t)s_last_time_ms) {
      persist_write_int(s_map_resource_id, s_last_time_ms);
    }
  }
}

static void window_load(Window *window) {
  light_enable(true);

  load_map(s_map_resource_id);
  s_kart_black = gbitmap_create_with_resource(RESOURCE_ID_KART_BLACK);
  s_kart_white = gbitmap_create_with_resource(RESOURCE_ID_KART_WHITE);
  s_kart_black2 = gbitmap_create_with_resource(RESOURCE_ID_KART_BLACK2);
  s_kart_white2 = gbitmap_create_with_resource(RESOURCE_ID_KART_WHITE2);
  s_kart_black3 = gbitmap_create_with_resource(RESOURCE_ID_KART_BLACK3);
  s_kart_white3 = gbitmap_create_with_resource(RESOURCE_ID_KART_WHITE3);
  s_tiles = gbitmap_create_with_resource(RESOURCE_ID_TILES);
  s_tile_data = gbitmap_get_data(s_tiles);
  s_sky = gbitmap_create_with_resource(RESOURCE_ID_SKY);
  s_font = gbitmap_create_with_resource(RESOURCE_ID_FONT);
  s_font_data = gbitmap_get_data(s_font);

  s_completed = false;
  time_ms_get_time(&s_start_time);
  s_pos = 1;

  s_kart = kart_create(0);
  for (int ii = 0; ii < NUM_OTHER_KARTS; ii++)
  {
    s_other_karts[ii] = kart_create(ii + 1);
  }
  Layer* window_layer = window_get_root_layer(window);
  s_window_bounds = layer_get_bounds(window_layer);
  GRect window_frame = layer_get_frame(window_layer);

  s_window_layer = layer_create(window_frame);
  layer_set_update_proc(s_window_layer, update_proc);
  layer_add_child(window_layer, s_window_layer);

  accel_data_service_subscribe(0, NULL);
  s_timer = app_timer_register(50, core_loop, NULL);
}

static void window_unload(Window *window) {
  app_timer_cancel(s_timer);
  accel_data_service_unsubscribe();

  layer_destroy(s_window_layer);

  kart_destroy(s_kart);
  for (int ii = 0; ii < NUM_OTHER_KARTS; ii++)
  {
    kart_destroy(s_other_karts[ii]);
  }

  gbitmap_destroy(s_font);
  gbitmap_destroy(s_sky);
  gbitmap_destroy(s_tiles);
  gbitmap_destroy(s_kart_white);
  gbitmap_destroy(s_kart_black);
  gbitmap_destroy(s_kart_white2);
  gbitmap_destroy(s_kart_black2);
  gbitmap_destroy(s_kart_white3);
  gbitmap_destroy(s_kart_black3);
  free(s_map);

  light_enable(false);
}

void start_race(uint32_t resource_id) {
  s_map_resource_id = resource_id;

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, false);
}

void end_race(void) {
  if (s_window != NULL) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
