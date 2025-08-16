#include <pebble.h>
#include "utils.h"

static Window* s_window;
static Layer* s_layer;
#define BUFFER_LEN (20)
static char s_buffer[BUFFER_LEN];

#define BBOX_DEBUG (false)
#define SETTINGS_VERSION_KEY (1)
#define SETTINGS_KEY (2)
#define ROUND (PBL_IF_ROUND_ELSE(true, false))

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
  settings.color_background   = COLOR_FALLBACK(GColorOxfordBlue,      GColorBlack);
  settings.color_time_text    = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);
  settings.color_corner_title = COLOR_FALLBACK(GColorVividCerulean,   GColorWhite);
  settings.color_corner_value = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);
  settings.color_separator    = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);
  settings.include_seconds = true;
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
  format_time(now, settings.include_seconds, s_buffer, BUFFER_LEN);
  graphics_draw_text(ctx, s_buffer, font, bbox, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void hsplit_rect(GContext* ctx, GRect bbox, GRect* upper, GRect* lower) {
  int upper_h = bbox.size.h * 5 / 12;
  *upper = GRect(
    bbox.origin.x,
    bbox.origin.y,
    bbox.size.w,
    upper_h
  );
  *lower = GRect(
    bbox.origin.x,
    bbox.origin.y + upper_h,
    bbox.size.w,
    bbox.size.h - upper_h
  );
  debug_bbox(ctx, *upper);
  debug_bbox(ctx, *lower);
}

static void draw_title(GContext* ctx, GRect bbox) {
  graphics_context_set_text_color(ctx, settings.color_corner_title);
  draw_text_midalign(ctx, s_buffer, bbox, GTextAlignmentCenter, false);
}

static void draw_value(GContext* ctx, GRect bbox) {
  graphics_context_set_text_color(ctx, settings.color_corner_value);
  draw_text_midalign(ctx, s_buffer, bbox, GTextAlignmentCenter, true);
}

static void draw_separator(GContext* ctx, GRect bbox, bool is_bot) {
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, settings.color_separator);
  int height = bbox.origin.y;
  if (is_bot) {
    height += bbox.size.h - 1;
  }
  int width = bbox.size.w;
  graphics_draw_line(ctx,
    GPoint(bbox.origin.x + width * 0.1, height),
    GPoint(bbox.origin.x + width * 0.9, height)
  );
}

static void draw_batt(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower);
  snprintf(s_buffer, BUFFER_LEN, "%s", "Battery");
  draw_title(ctx, upper);
  BatteryChargeState bcs = battery_state_service_peek();
  snprintf(s_buffer, BUFFER_LEN, "%d%%", bcs.charge_percent);
  draw_value(ctx, lower);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void draw_date(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower);
  snprintf(s_buffer, BUFFER_LEN, "%s", "Date");
  draw_title(ctx, upper);
  snprintf(s_buffer, BUFFER_LEN, "%s/%s", "xx", "yy");  // TODO (mm/dd and dd/mm)
  draw_value(ctx, lower);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void draw_steps(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower);
  snprintf(s_buffer, BUFFER_LEN, "%s", "Steps");
  draw_title(ctx, upper);
  snprintf(s_buffer, BUFFER_LEN, "%s", "zzzz"); // TODO
  draw_value(ctx, lower);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void draw_temp(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower);
  snprintf(s_buffer, BUFFER_LEN, "%s", "Weather");
  draw_title(ctx, upper);
  snprintf(s_buffer, BUFFER_LEN, "%s°", "--"); // TODO
  draw_value(ctx, lower);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void update_layer(Layer* layer, GContext* ctx) {
  time_t temp = time(NULL);
  struct tm* now = localtime(&temp);

  GRect bounds = layer_get_unobstructed_bounds(layer);
  graphics_context_set_fill_color(ctx, settings.color_background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  GRect visible = bounds;
  if (ROUND) {
    int w = bounds.size.w;
    int h = bounds.size.h;
    visible = GRect(
      bounds.origin.x + w * 0.1,
      bounds.origin.y + h * 0.1,
      w * 0.8,
      h * 0.8
    );
  }

  GPoint center = grect_center_point(&visible);
  GFont main_font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  GRect time_bbox = rect_from_center(center, GSize(visible.size.w, 49 + 14));
  if (visible.size.h < 200) {
    // TODO find a monospace font. Time jitters left/right
    main_font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    time_bbox = rect_from_center(center, GSize(visible.size.w, 34 + 10));
  }
  draw_time(ctx, now, main_font, time_bbox);

  int complication_avail_height = (visible.size.h - time_bbox.size.h) / 2;
  int complication_height = complication_avail_height * 3 / 4;
  GSize complication_size = GSize(visible.size.w / 2, complication_height);
  int left  = visible.origin.x + visible.size.w / 4;
  int right = visible.origin.x + visible.size.w * 3 / 4;
  int top = visible.origin.y                 + complication_avail_height / 2;
  int bot = visible.origin.y + visible.size.h - complication_avail_height / 2;
  bool sep_bot = true;
  bool sep_top = false;

  draw_batt(ctx, rect_from_center(GPoint(left, top), complication_size), sep_bot);
  draw_date(ctx, rect_from_center(GPoint(right, top), complication_size), sep_bot);
  draw_steps(ctx, rect_from_center(GPoint(left, bot), complication_size), sep_top);
  draw_temp(ctx, rect_from_center(GPoint(right, bot), complication_size), sep_top);
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
  // TODO: need to re-subscribe when this value changes
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
