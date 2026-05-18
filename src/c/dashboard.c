#include <stdlib.h>

#include <pebble.h>

#include "utils.h"

static Window* s_window = NULL;
static Layer* s_layer = NULL;
#define BUFFER_LEN (20)
static char s_buffer[BUFFER_LEN];
static GFont s_font_xl = NULL;
static GFont s_font_lg = NULL;
static GFont s_font_md = NULL;
static GFont s_font_sm = NULL;
static TimeUnits s_time_units = SECOND_UNIT;
static int s_num_remaining_awake_frames = -1;

#define BBOX_DEBUG (false)
#define SETTINGS_VERSION_KEY (1)
#define SETTINGS_KEY (2)
#define INVALID_TEMP (9999)

#define SECONDS_NEVER (0)
#define SECONDS_ALWAYS (1)
#define SECONDS_ON_WAKE (2)

typedef struct ClaySettings {
  GColor color_background;
  GColor color_time_text;
  GColor color_corner_title;
  GColor color_corner_value;
  GColor color_separator;

  uint8_t include_seconds;
  bool month_first;
  bool temperature_in_celsius;
  uint8_t reserved[40]; // for later growth
} __attribute__((__packed__)) ClaySettings;

ClaySettings s_settings;

static void default_settings() {
  s_settings.color_background   = COLOR_FALLBACK(GColorOxfordBlue,      GColorBlack);
  s_settings.color_time_text    = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);
  s_settings.color_corner_title = COLOR_FALLBACK(GColorVividCerulean,   GColorWhite);
  s_settings.color_corner_value = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);
  s_settings.color_separator    = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);

  s_settings.include_seconds        = SECONDS_ALWAYS;
  s_settings.month_first            = false;
  s_settings.temperature_in_celsius = true;
}

typedef struct Weather {
  int temp_deci_c;
} Weather;

Weather s_weather_now;

static void debug_bbox(GContext* ctx, GRect bbox) {
  if (BBOX_DEBUG) {
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, bbox);
  }
}

static int draw_time(GContext* ctx, struct tm* now, GRect visible) {
  GPoint center = grect_center_point(&visible);
  GFont font;
  GRect bbox = rect_from_center(center, GSize(visible.size.w, 48 * 5 / 4));
  int shift_up = 0;
  if (s_time_units == SECOND_UNIT) {
    font = s_font_lg;
    shift_up = 1;
  } else {
    font = s_font_xl;
    shift_up = 14;
  }
  debug_bbox(ctx, bbox);
  graphics_context_set_text_color(ctx, s_settings.color_time_text);
  format_time(now, (s_time_units == SECOND_UNIT), s_buffer, BUFFER_LEN);
  draw_text(ctx, s_buffer, font, bbox, GTextAlignmentCenter, shift_up);
  return bbox.size.h;
}

static void hsplit_rect(GContext* ctx, GRect bbox, GRect* upper, GRect* lower, bool big_on_bot) {
  int upper_h;
  if (big_on_bot) {
    upper_h = bbox.size.h * 5 / 12;
  } else {
    upper_h = bbox.size.h * 7 / 12;
  }
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
  graphics_context_set_text_color(ctx, s_settings.color_corner_title);
  draw_text(ctx, s_buffer, s_font_sm, bbox, GTextAlignmentCenter, 0);
}

static void draw_value(GContext* ctx, GRect bbox) {
  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  draw_text(ctx, s_buffer, s_font_md, bbox, GTextAlignmentCenter, 0);
}

static void draw_separator(GContext* ctx, GRect bbox, bool is_bot) {
  graphics_context_set_stroke_width(ctx, 3);
  graphics_context_set_stroke_color(ctx, s_settings.color_separator);
  int height = bbox.origin.y;
  if (is_bot) {
    height += bbox.size.h - 1;
  }
  int width = bbox.size.w;
  graphics_draw_line(ctx,
    GPoint(bbox.origin.x + width * 1 / 10, height),
    GPoint(bbox.origin.x + width * 9 / 10, height)
  );
}

static void draw_batt(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower, true);

  snprintf(s_buffer, BUFFER_LEN, "%s", "Battery");
  draw_title(ctx, upper);

  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  BatteryChargeState bcs = battery_state_service_peek();
  snprintf(s_buffer, BUFFER_LEN, "%d%%", bcs.charge_percent);
  draw_text(ctx, s_buffer, s_font_md, lower, GTextAlignmentCenter, 0);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void draw_date(GContext* ctx, GRect bbox, bool sep_on_bot, struct tm* now) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower, true);

  strftime(s_buffer, BUFFER_LEN, "%A", now);
  draw_title(ctx, upper);

  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  format_date(now, s_settings.month_first, s_buffer, BUFFER_LEN);
  draw_text(ctx, s_buffer, s_font_md, lower, GTextAlignmentCenter, 0);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void draw_steps(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower, false);
  snprintf(s_buffer, BUFFER_LEN, "%s", "Steps");
  draw_title(ctx, lower);
  int steps = health_service_sum_today(HealthMetricStepCount);
  snprintf(s_buffer, BUFFER_LEN, "%d", steps);
  draw_value(ctx, upper);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void draw_temp(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower, false);
  snprintf(s_buffer, BUFFER_LEN, "%s", "Weather");
  draw_title(ctx, lower);
  if (s_weather_now.temp_deci_c == INVALID_TEMP) {
    snprintf(s_buffer, BUFFER_LEN, "%s°", "--");
  } else if (s_settings.temperature_in_celsius) {
    snprintf(s_buffer, BUFFER_LEN, "%d.%d°c", s_weather_now.temp_deci_c / 10, s_weather_now.temp_deci_c % 10);
  } else {
    int temp_deci_f = s_weather_now.temp_deci_c * 9 / 5 + 320;
    snprintf(s_buffer, BUFFER_LEN, "%d.%d°f", temp_deci_f / 10, temp_deci_f % 10);
  }
  draw_value(ctx, upper);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void tick_handler(struct tm* now, TimeUnits units_changed) {
  if (s_layer) { layer_mark_dirty(s_layer); }

  if (now->tm_min % 30 == 0 && now->tm_sec == 0) {
    // send an empty message. that means "give me weather!"
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_uint8(iter, 0, 0);
    app_message_outbox_send();
  }
}

static void handle_accel_tap(AccelAxisType axis, int32_t direction) {
  // TODO https://forum.repebble.com/t/show-seconds-only-when-backlight-is-active/565
  if (s_settings.include_seconds == SECONDS_ON_WAKE) {
    s_num_remaining_awake_frames = 10;  // 10 frames = 10 seconds because of the units we select
    s_time_units = SECOND_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  }
}

static void tick_resub() {
  if (s_settings.include_seconds == SECONDS_NEVER && s_time_units != MINUTE_UNIT) {
    s_time_units = MINUTE_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  } else if (s_settings.include_seconds == SECONDS_ON_WAKE && s_num_remaining_awake_frames == 0) {
    s_time_units = MINUTE_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  } else if (s_settings.include_seconds == SECONDS_ALWAYS && s_time_units != SECOND_UNIT) {
    s_time_units = SECOND_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  }

  if (s_num_remaining_awake_frames > -1) {
    s_num_remaining_awake_frames--;
  }
}

static void update_layer(Layer* layer, GContext* ctx) {
  time_t temp = time(NULL);
  struct tm* now = localtime(&temp);

  GRect bounds = layer_get_bounds(layer);
  GRect visible = layer_get_unobstructed_bounds(layer);
  bool timeline_quick_view = visible.size.h < bounds.size.h - 1;

  graphics_context_set_fill_color(ctx, s_settings.color_background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int time_bbox_height = draw_time(ctx, now, bounds);

  int complication_height = (bounds.size.h - time_bbox_height) / 2 - 10;
  GSize complication_size = GSize(bounds.size.w / 2, complication_height);
  int left  = bounds.origin.x + bounds.size.w / 4;
  int right = bounds.origin.x + bounds.size.w * 3 / 4;
  int top = bounds.origin.y                  + complication_height / 2;
  int bot = bounds.origin.y + bounds.size.h - complication_height / 2;
  bool sep_on_bot = true;
  bool sep_on_top = false;

  draw_batt(ctx, rect_from_center(GPoint(left,  top), complication_size), sep_on_bot);
  draw_date(ctx, rect_from_center(GPoint(right, top), complication_size), sep_on_bot, now);
  if (!timeline_quick_view) {
    draw_steps(ctx, rect_from_center(GPoint(left,  bot), complication_size), sep_on_top);
    draw_temp(ctx, rect_from_center(GPoint(right, bot), complication_size), sep_on_top);
  }

  tick_resub();
}

static void window_load(Window* window) {
  Layer* window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(s_window, s_settings.color_background);
  s_layer = layer_create(bounds);
  layer_set_update_proc(s_layer, update_layer);
  layer_add_child(window_layer, s_layer);
}

static void window_unload(Window* window) {
  if (s_layer) { layer_destroy(s_layer); }
}

static void load_settings() {
  default_settings();
  // If we need a backwards incompatible version of settings, check SETTINGS_VERSION_KEY and migrate
  persist_read_data(SETTINGS_KEY, &s_settings, sizeof(ClaySettings));
}

static void save_settings() {
  persist_write_int(SETTINGS_VERSION_KEY, 1);
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(ClaySettings));
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_color_background             ))) { s_settings.color_background         = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_time_text              ))) { s_settings.color_time_text          = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_corner_title           ))) { s_settings.color_corner_title       = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_corner_value           ))) { s_settings.color_corner_value       = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_separator              ))) { s_settings.color_separator          = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_include_seconds              ))) { s_settings.include_seconds          = atoi(t->value->cstring); }
  if ((t = dict_find(iter, MESSAGE_KEY_month_first                  ))) { s_settings.month_first              = t->value->int8; }
  if ((t = dict_find(iter, MESSAGE_KEY_temperature_in_celsius       ))) { s_settings.temperature_in_celsius   = t->value->int8; }
  if ((t = dict_find(iter, MESSAGE_KEY_weather_now_temp_deci_c      ))) { s_weather_now.temp_deci_c           = t->value->int32; }
  save_settings();
  // Update the display based on new settings
  layer_mark_dirty(window_get_root_layer(s_window));
  if (s_settings.include_seconds == SECONDS_ALWAYS) {
    s_time_units = SECOND_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  } else {
    s_time_units = MINUTE_UNIT;  // minutes until an event shortens it with a timer to return to minutes
    tick_timer_service_subscribe(s_time_units, tick_handler);
  }
}

static void init(void) {
  s_font_xl = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_68));
  s_font_lg = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_50));
  s_font_md = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_34));
  s_font_sm = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_20));

  tick_timer_service_subscribe(s_time_units, tick_handler);
  accel_tap_service_subscribe(handle_accel_tap);
  load_settings();
  s_weather_now.temp_deci_c = INVALID_TEMP;
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  if (s_window) { window_destroy(s_window); }
  if (s_font_xl) { fonts_unload_custom_font(s_font_xl); }
  if (s_font_lg) { fonts_unload_custom_font(s_font_lg); }
  if (s_font_md) { fonts_unload_custom_font(s_font_md); }
  if (s_font_sm) { fonts_unload_custom_font(s_font_sm); }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
