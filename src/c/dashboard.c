#include <pebble.h>
#include "utils.h"

static Window* s_window;
static Layer* s_layer;
#define BUFFER_LEN (20)
static char s_buffer[BUFFER_LEN];

#define BBOX_DEBUG (false)
#define SETTINGS_VERSION_KEY (1)
#define SETTINGS_KEY (2)

typedef struct ClaySettings {
  GColor color_background;
  GColor color_time_text;
  bool include_seconds;
} __attribute__((__packed__)) ClaySettings;

ClaySettings settings;

static void default_settings() {
  settings.color_background = GColorOxfordBlue;
  settings.color_time_text = GColorOrange;
  settings.include_seconds = true;
}

static void format_time(struct tm* now, char* buf, int buf_len) {
  if (clock_is_24h_style()) {
    if (settings.include_seconds) {
      strftime(buf, buf_len, "%H:%M:%S", now);
    } else {
      strftime(buf, buf_len, "%H:%M", now);
    }
  } else {
    if (settings.include_seconds) {
      strftime(buf, buf_len, "%I:%M:%S", now);
    } else {
      strftime(buf, buf_len, "%I:%M", now);
    }
  }
}

static void draw_time(GContext* ctx, struct tm* now, GRect bounds) {
  GPoint center = grect_center_point(&bounds);
  GFont font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  GRect bbox = rect_from_midpoint(center, GSize(bounds.size.w, 49 + 14));
  if (BBOX_DEBUG) {
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, bbox);
  }
  graphics_context_set_text_color(ctx, settings.color_time_text);
  format_time(now, s_buffer, BUFFER_LEN);
  graphics_draw_text(ctx, s_buffer, font, bbox, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void update_layer(Layer* layer, GContext* ctx) {
  time_t temp = time(NULL);
  struct tm* now = localtime(&temp);

  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, settings.color_background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  draw_time(ctx, now, bounds);
}

static void window_load(Window* window) {
  Layer* window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(s_window, settings.color_background);
  s_layer = layer_create(bounds);
  layer_set_update_proc(s_layer, update_layer);
  layer_add_child(window_layer, s_layer);
}

static void window_unload(Window* window) {
  layer_destroy(s_layer);
}

static void tick_handler(struct tm* now, TimeUnits units_changed) {
  layer_mark_dirty(window_get_root_layer(s_window));
}

static void load_settings() {
  default_settings();
  // If we need a new version of settings, check SETTINGS_VERSION_KEY and migrate
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void save_settings() {
  persist_write_int(SETTINGS_VERSION_KEY, 1);
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_color_background       ))) { settings.color_background         = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_time_text        ))) { settings.color_time_text         = GColorFromHEX(t->value->int32); }
  save_settings();
  // Update the display based on new settings
  layer_mark_dirty(window_get_root_layer(s_window));
}

static void init(void) {
  load_settings();
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(settings.include_seconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  if (s_window) {
    window_destroy(s_window);
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
