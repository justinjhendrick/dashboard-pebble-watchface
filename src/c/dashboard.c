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
static time_t s_last_request_sent = 0;
#define INIT_WEATHER_RETRY_SECONDS (5)
#define MAX_WEATHER_RETRY_SECONDS (60 * 60)
static int s_weather_retry_seconds = INIT_WEATHER_RETRY_SECONDS;

#define DEBUG_BBOX (false)
#define DEBUG_TIME (false)

#define SETTINGS_VERSION_KEY (1)
#define SETTINGS_KEY (2)
#define INVALID_TEMP (9999)

#define SECONDS_NEVER (0)
#define SECONDS_ALWAYS (1)
#define SECONDS_ON_WAKE (2)

#define DEFAULT_TEMPERATURE_TENTHS (true)
#define DEFAULT_LEADING_ZERO_IN_12H (false)
#define DEFAULT_AM_PM_IN_12H (true)
#define DEFAULT_LEADING_ZERO_IN_DATE (true)
#define SETTINGS_RESERVED_BYTES (36)

typedef struct ClaySettings {
  // Do not change the order!
  // Preserved across updates.
  GColor color_background;
  GColor color_time_text;
  GColor color_corner_title;
  GColor color_corner_value;
  GColor color_separator;

  uint8_t include_seconds;
  bool month_first;
  bool temperature_in_celsius;
  // Above was present in Settings v1
  bool temperature_tenths;
  bool leading_zero_hour_in_12h;
  bool am_pm_in_12h;
  bool leading_zero_in_date;
  // Above was present in Settings v2

  // for later growth
  uint8_t reserved[SETTINGS_RESERVED_BYTES];
} __attribute__((__packed__)) ClaySettings;

ClaySettings s_settings;

static void default_settings() {
  s_settings.color_background   = COLOR_FALLBACK(GColorOxfordBlue,      GColorBlack);
  s_settings.color_time_text    = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);
  s_settings.color_corner_title = COLOR_FALLBACK(GColorVividCerulean,   GColorWhite);
  s_settings.color_corner_value = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);
  s_settings.color_separator    = COLOR_FALLBACK(GColorChromeYellow,    GColorWhite);

  s_settings.include_seconds = SECONDS_ALWAYS;
  s_settings.month_first = false;
  s_settings.temperature_in_celsius = true;
  s_settings.temperature_tenths = DEFAULT_TEMPERATURE_TENTHS;
  s_settings.leading_zero_hour_in_12h = DEFAULT_LEADING_ZERO_IN_12H;
  s_settings.am_pm_in_12h = DEFAULT_AM_PM_IN_12H;
  s_settings.leading_zero_in_date = DEFAULT_LEADING_ZERO_IN_DATE;

  // don't want to save undefined memory to storage
  // But Settings v1 didn't have this. Settings v2 started it.
  for (int i = 0; i < SETTINGS_RESERVED_BYTES; i++) {
    s_settings.reserved[i] = 0;
  }
}

typedef struct Weather {
  int temp_deci_c;
} Weather;

Weather s_weather_now;

static void debug_bbox(GContext* ctx, GRect bbox) {
  if (DEBUG_BBOX) {
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, bbox);
  }
}

static int draw_time(GContext* ctx, struct tm* now, GRect visible) {
  graphics_context_set_text_color(ctx, s_settings.color_time_text);
  bool include_seconds = (s_time_units == SECOND_UNIT);
  bool include_am_pm = (!clock_is_24h_style() && s_settings.am_pm_in_12h);

  GPoint center = grect_center_point(&visible);
  GRect bbox = rect_from_center(center, GSize(visible.size.w, 48 * 5 / 4));

  GRect left;
  left.origin = bbox.origin;
  left.size.w = bbox.size.w * 6 / 7;
  left.size.h = bbox.size.h;

  GRect right;
  right.origin.x = bbox.origin.x + left.size.w;
  right.origin.y = bbox.origin.y;
  right.size.h = bbox.size.h;
  right.size.w = visible.size.w - left.size.w;

  GRect up_right;
  up_right.origin = right.origin;
  up_right.size.h = bbox.size.h / 2;
  up_right.size.w = right.size.w;

  GRect down_right;
  down_right.origin.x = right.origin.x;
  down_right.origin.y = right.origin.y + bbox.size.h / 2;
  down_right.size.h = bbox.size.h / 2;
  down_right.size.w = right.size.w;

  // Draw Hours & Minutes
  format_time(now, s_settings.leading_zero_hour_in_12h, s_buffer, BUFFER_LEN);
  if (include_seconds || include_am_pm) {
    debug_bbox(ctx, left);
    debug_bbox(ctx, up_right);
    debug_bbox(ctx, down_right);
    draw_text(ctx, s_buffer, s_font_xl, left, GTextAlignmentRight, 14);
  } else {
    debug_bbox(ctx, bbox);
    draw_text(ctx, s_buffer, s_font_xl, bbox, GTextAlignmentCenter, 14);
  }

  if (include_seconds) {
    if (include_am_pm) {
      // Up right has A/P
      strftime(s_buffer, BUFFER_LEN, "%p", now);
      s_buffer[1] = '\0'; // no M. just A/P.
      draw_text(ctx, s_buffer, s_font_sm, up_right, GTextAlignmentCenter, 0);
    }
    // Down right has seconds
    strftime(s_buffer, BUFFER_LEN, "%S", now);
    draw_text(ctx, s_buffer, s_font_sm, down_right, GTextAlignmentCenter, 0);
  } else if (include_am_pm) {
    strftime(s_buffer, BUFFER_LEN, "%p", now);
    s_buffer[1] = '\0'; // no M. just A/P.
    if (now->tm_hour < 12) {
      // Up right has A
      draw_text(ctx, s_buffer, s_font_sm, up_right, GTextAlignmentCenter, 0);
    } else {
      // Down right has P
      draw_text(ctx, s_buffer, s_font_sm, down_right, GTextAlignmentCenter, 0);
    }
  }
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

  strftime(s_buffer, BUFFER_LEN, "%a", now);
  draw_title(ctx, upper);

  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  format_date(now, s_settings.month_first, s_settings.leading_zero_in_date, s_buffer, BUFFER_LEN);
  draw_text(ctx, s_buffer, s_font_md, lower, GTextAlignmentCenter, 0);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void draw_steps(GContext* ctx, GRect bbox, bool sep_on_bot) {
  GRect upper, lower;
  hsplit_rect(ctx, bbox, &upper, &lower, false);
  int steps = health_service_sum_today(HealthMetricStepCount);
  if (steps >= 10000) {
    snprintf(s_buffer, BUFFER_LEN, "%s", "kSteps");
    draw_title(ctx, lower);
    snprintf(s_buffer, BUFFER_LEN, "%d.%d", steps / 1000, (steps % 1000) / 100);
  } else {
    snprintf(s_buffer, BUFFER_LEN, "%s", "Steps");
    draw_title(ctx, lower);
    snprintf(s_buffer, BUFFER_LEN, "%d", steps);
  }
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
    if (s_settings.temperature_tenths) {
      snprintf(s_buffer, BUFFER_LEN, "%d.%d°c", s_weather_now.temp_deci_c / 10, s_weather_now.temp_deci_c % 10);
    } else {
      snprintf(s_buffer, BUFFER_LEN, "%d°c", (s_weather_now.temp_deci_c + 5) / 10);
    }
  } else {
    int temp_deci_f = s_weather_now.temp_deci_c * 9 / 5 + 320;
    if (s_settings.temperature_tenths) {
      snprintf(s_buffer, BUFFER_LEN, "%d.%d°f", temp_deci_f / 10, temp_deci_f % 10);
    } else {
      snprintf(s_buffer, BUFFER_LEN, "%d°f", (temp_deci_f + 5) / 10);
    }
  }
  draw_value(ctx, upper);
  draw_separator(ctx, bbox, sep_on_bot);
}

static void maybe_request_weather() {
  time_t now = time(NULL);
  bool should_request = false;
  if (s_weather_now.temp_deci_c == INVALID_TEMP && now >= s_last_request_sent + s_weather_retry_seconds) {
    // Retry multiple times while temp is invalid
    should_request = true;

    // Exponential backoff. Conserve battery in case GPS is disabled.
    s_weather_retry_seconds *= 2;
    if (s_weather_retry_seconds >= MAX_WEATHER_RETRY_SECONDS) {
      s_weather_retry_seconds = MAX_WEATHER_RETRY_SECONDS;
    }
  }
  if (now >= s_last_request_sent + 30 * 60) {
    // Get fresh data on a regular cadence
    should_request = true;
  }

  if (should_request) {
    // send an empty message. that means "give me weather!"
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_uint8(iter, 0, 0);
    app_message_outbox_send();
    s_last_request_sent = now;
  }
}

static void tick_handler(struct tm* now, TimeUnits units_changed) {
  if (s_layer) { layer_mark_dirty(s_layer); }
  maybe_request_weather();
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
  if (DEBUG_TIME) {
    now->tm_min = now->tm_sec;
    now->tm_hour = now->tm_sec % 24;
    now->tm_mday = now->tm_sec % 31 + 1;
    now->tm_mon = now->tm_sec % 12;
    now->tm_wday = now->tm_wday % 7;
  }

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

  int32_t loaded_version = persist_read_int(SETTINGS_VERSION_KEY);
  persist_read_data(SETTINGS_KEY, &s_settings, sizeof(ClaySettings));

  // Check for older versions of settings and migrate them
  if (loaded_version == 1) {
    // these were in an undefined array before
    s_settings.temperature_tenths = DEFAULT_TEMPERATURE_TENTHS;
    s_settings.leading_zero_hour_in_12h = DEFAULT_LEADING_ZERO_IN_12H;
    s_settings.am_pm_in_12h = DEFAULT_AM_PM_IN_12H;
    s_settings.leading_zero_in_date = DEFAULT_LEADING_ZERO_IN_DATE;
  }
}

static void save_settings() {
  persist_write_int(SETTINGS_VERSION_KEY, 2);
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(ClaySettings));
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_color_background             ))) { s_settings.color_background          = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_time_text              ))) { s_settings.color_time_text           = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_corner_title           ))) { s_settings.color_corner_title        = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_corner_value           ))) { s_settings.color_corner_value        = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_separator              ))) { s_settings.color_separator           = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_include_seconds              ))) { s_settings.include_seconds           = atoi(t->value->cstring); }
  if ((t = dict_find(iter, MESSAGE_KEY_month_first                  ))) { s_settings.month_first               = t->value->int8; }
  if ((t = dict_find(iter, MESSAGE_KEY_temperature_in_celsius       ))) { s_settings.temperature_in_celsius    = t->value->int8; }
  if ((t = dict_find(iter, MESSAGE_KEY_temperature_tenths           ))) { s_settings.temperature_tenths        = t->value->int8; }
  if ((t = dict_find(iter, MESSAGE_KEY_leading_zero_hour_in_12h     ))) { s_settings.leading_zero_hour_in_12h  = t->value->int8; }
  if ((t = dict_find(iter, MESSAGE_KEY_am_pm_in_12h                 ))) { s_settings.am_pm_in_12h              = t->value->int8; }
  if ((t = dict_find(iter, MESSAGE_KEY_leading_zero_in_date         ))) { s_settings.leading_zero_in_date      = t->value->int8; }

  if ((t = dict_find(iter, MESSAGE_KEY_weather_now_temp_deci_c))) {
    s_weather_now.temp_deci_c = t->value->int32;
    s_weather_retry_seconds = INIT_WEATHER_RETRY_SECONDS;
  }

  save_settings();
  // Update the display based on new settings
  if (s_layer) { layer_mark_dirty(s_layer); }
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
  s_font_sm = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_22));

  load_settings();
  tick_timer_service_subscribe(s_time_units, tick_handler);
  accel_tap_service_subscribe(handle_accel_tap);
  s_weather_now.temp_deci_c = INVALID_TEMP;
  s_last_request_sent = time(NULL);  // because js sends weather on "ready" event
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(1024, 64);

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
