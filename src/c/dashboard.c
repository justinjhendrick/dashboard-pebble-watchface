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
  GColor color_corner_title;
  GColor color_corner_value;
  GColor color_separator;
  bool include_seconds;
  uint8_t reserved[40]; // for later growth
} __attribute__((__packed__)) ClaySettings;

ClaySettings settings;

static void default_settings() {
  settings.color_background = GColorOxfordBlue;
  settings.color_time_text = GColorChromeYellow;
  settings.color_corner_title = GColorVividCerulean;
  settings.color_corner_value = GColorChromeYellow;
  settings.color_separator = GColorChromeYellow;
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

static void debug_bbox(GContext* ctx, GRect bbox) {
  if (BBOX_DEBUG) {
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, bbox);
  }
}

static void draw_time(GContext* ctx, struct tm* now, GFont font, GRect bbox) {
  debug_bbox(ctx, bbox);
  graphics_context_set_text_color(ctx, settings.color_time_text);
  format_time(now, s_buffer, BUFFER_LEN);
  graphics_draw_text(ctx, s_buffer, font, bbox, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void draw_batt(GContext* ctx, GRect bbox) {
  GRect upper = GRect(
    bbox.origin.x,
    bbox.origin.y,
    bbox.size.w,
    bbox.size.h / 3
  );
  debug_bbox(ctx, upper);
  graphics_context_set_text_color(ctx, settings.color_corner_title);
  snprintf(s_buffer, BUFFER_LEN, "%s", "Battery");
  draw_text_midalign(ctx, s_buffer, upper, GTextAlignmentCenter, false);

  GRect lower = GRect(
    bbox.origin.x,
    bbox.origin.y + bbox.size.h / 3,
    bbox.size.w,
    bbox.size.h * 2 / 3
  );
  debug_bbox(ctx, lower);
  graphics_context_set_text_color(ctx, settings.color_corner_value);
  BatteryChargeState bcs = battery_state_service_peek();
  snprintf(s_buffer, BUFFER_LEN, "%d%%", bcs.charge_percent);
  draw_text_topalign(ctx, s_buffer, lower, GTextAlignmentCenter, true);

  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, settings.color_separator);
  graphics_draw_line(ctx,
    GPoint(lower.origin.x                + 10, lower.origin.y + lower.size.h),
    GPoint(lower.origin.x + lower.size.w - 10, lower.origin.y + lower.size.h)
  );
}

static void update_layer(Layer* layer, GContext* ctx) {
  time_t temp = time(NULL);
  struct tm* now = localtime(&temp);

  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, settings.color_background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  GPoint center = grect_center_point(&bounds);
  GFont main_font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  GRect time_bbox = rect_from_midpoint(center, GSize(bounds.size.w, 49 + 14));
  draw_time(ctx, now, main_font, time_bbox);

  int complication_avail_height = (bounds.size.h - time_bbox.size.h) / 2;
  GPoint tl_center = GPoint(
    bounds.origin.x + bounds.size.w / 4,
    bounds.origin.y + complication_avail_height / 2
  );
  GRect tl_corner = rect_from_midpoint(tl_center, GSize(bounds.size.w / 2, complication_avail_height * 3 / 4));
  draw_batt(ctx, tl_corner);
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
  if ((t = dict_find(iter, MESSAGE_KEY_color_time_text        ))) { settings.color_time_text          = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_corner_title     ))) { settings.color_corner_title       = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_corner_value     ))) { settings.color_corner_value       = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_separator        ))) { settings.color_separator          = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_include_seconds        ))) { settings.include_seconds          = t->value->int8; }
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
