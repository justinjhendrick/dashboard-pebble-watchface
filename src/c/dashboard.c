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
static time_t s_last_wake = 0;
static time_t s_last_request_sent = 0;
#define INIT_WEATHER_RETRY_SECONDS (5)
#define MAX_WEATHER_RETRY_SECONDS (60 * 60)
static int s_weather_retry_seconds = INIT_WEATHER_RETRY_SECONDS;

#define NUM_ERROR_CODES (4)
static int s_error_codes[NUM_ERROR_CODES] = {0};
static int s_error_code_idx = 0;

#define DEBUG_BBOX (false)
#define DEBUG_TIME (false)

#define SETTINGS_VERSION_KEY (1)
#define SETTINGS_KEY (2)

#define INVALID_TEMP (9999)
#define INVALID_RAIN (-1)
#define INVALID_TIME (0)

#define SECONDS_NEVER (0)
#define SECONDS_ALWAYS (1)
#define SECONDS_ON_WAKE (2)

#define MODULE_NONE (0)
#define MODULE_BATTERY (1)
#define MODULE_DATE (2)
#define MODULE_STEPS (3)
#define MODULE_TEMP_NOW (4)
#define MODULE_HEART_RATE (5)
#define MODULE_RAIN_1H (6)
#define MODULE_RAIN_6H (7)
#define MODULE_SUNRISE (8)
#define MODULE_SUNSET (9)
#define MODULE_WATCH_STATUS (10)
#define MODULE_ERROR_CODES (11)

// More Module Ideas:
// WEATHER
//   UV index
//     this is hard because api.met.no only gives clear-sky UV index, maybe only for solar noon too?
//   Temperature high/low today
//     this is kinda hard because api.met.no doesn't split on day boundaries for me
//   Wind direction & speed now
//     not sure if api.met.no has enough granularity for this to be meaningful, especially near mountains

#define DEFAULT_TEMPERATURE_TENTHS (true)
#define DEFAULT_LEADING_ZERO_IN_12H (false)
#define DEFAULT_AM_PM_IN_12H (true)
#define DEFAULT_LEADING_ZERO_IN_DATE (true)
#define DEFAULT_MODULE_TOP_LEFT (MODULE_BATTERY)
#define DEFAULT_MODULE_TOP_RIGHT (MODULE_DATE)
#define DEFAULT_MODULE_BOT_LEFT (MODULE_STEPS)
#define DEFAULT_MODULE_BOT_RIGHT (MODULE_TEMP_NOW)
#define SETTINGS_RESERVED_BYTES (32)

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
  uint8_t module_top_left;
  uint8_t module_top_right;
  uint8_t module_bot_left;
  uint8_t module_bot_right;
  // Above was present in Settings v3

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

  s_settings.module_top_left = DEFAULT_MODULE_TOP_LEFT;
  s_settings.module_top_right = DEFAULT_MODULE_TOP_RIGHT;
  s_settings.module_bot_left = DEFAULT_MODULE_BOT_LEFT;
  s_settings.module_bot_right = DEFAULT_MODULE_BOT_RIGHT;

  // don't want to save undefined memory to storage
  // But Settings v1 didn't have this. Settings v2 started it.
  for (int i = 0; i < SETTINGS_RESERVED_BYTES; i++) {
    s_settings.reserved[i] = 0;
  }
}

typedef struct Weather {
  int temp_deci_c;
  int rain_1h_dmm;
  int rain_6h_dmm;
  time_t sunrise;
  time_t sunset;
} Weather;

Weather s_weather;

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

static void hsplit_rect(GContext* ctx, GRect bbox, GRect* value, GRect* title, bool title_on_top, bool hide_title) {
  if (hide_title) {
    *value = bbox;
    *title = GRect(0, 0, 0, 0);
    return;
  }
  GRect upper, lower;
  int upper_h;
  if (title_on_top) {
    upper_h = bbox.size.h * 5 / 12;
  } else {
    upper_h = bbox.size.h * 7 / 12;
  }
  upper = GRect(
    bbox.origin.x,
    bbox.origin.y,
    bbox.size.w,
    upper_h
  );
  lower = GRect(
    bbox.origin.x,
    bbox.origin.y + upper_h,
    bbox.size.w,
    bbox.size.h - upper_h
  );
  debug_bbox(ctx, upper);
  debug_bbox(ctx, lower);
  if (title_on_top) {
    *title = upper;
    *value = lower;
  } else {
    *value = upper;
    *title = lower;
  }
}

static void draw_title(GContext* ctx, GRect bbox) {
  graphics_context_set_text_color(ctx, s_settings.color_corner_title);
  draw_text(ctx, s_buffer, s_font_sm, bbox, GTextAlignmentCenter, 0);
}

static void draw_value(GContext* ctx, GRect bbox) {
  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  draw_text(ctx, s_buffer, s_font_md, bbox, GTextAlignmentCenter, 0);
}

static void draw_separator(GContext* ctx, GRect bbox, bool title_on_top) {
  graphics_context_set_stroke_width(ctx, 3);
  graphics_context_set_stroke_color(ctx, s_settings.color_separator);
  int height = bbox.origin.y;
  if (title_on_top) {
    height += bbox.size.h - 1;
  }
  int width = bbox.size.w;
  graphics_draw_line(ctx,
    GPoint(bbox.origin.x + width * 1 / 10, height),
    GPoint(bbox.origin.x + width * 9 / 10, height)
  );
}

static void draw_batt(GContext* ctx, GRect value, GRect title) {
  snprintf(s_buffer, BUFFER_LEN, "%s", "Battery");
  draw_title(ctx, title);

  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  BatteryChargeState bcs = battery_state_service_peek();
  snprintf(s_buffer, BUFFER_LEN, "%d%%", bcs.charge_percent);
  draw_text(ctx, s_buffer, s_font_md, value, GTextAlignmentCenter, 0);
}

static void draw_date(GContext* ctx, GRect value, GRect title, struct tm* now) {
  strftime(s_buffer, BUFFER_LEN, "%a", now);
  draw_title(ctx, title);

  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  format_date(now, s_settings.month_first, s_settings.leading_zero_in_date, s_buffer, BUFFER_LEN);
  draw_text(ctx, s_buffer, s_font_md, value, GTextAlignmentCenter, 0);
}

static void draw_steps(GContext* ctx, GRect value, GRect title) {
  int steps = health_service_sum_today(HealthMetricStepCount);
  if (steps >= 10000) {
    snprintf(s_buffer, BUFFER_LEN, "%s", "kSteps");
    draw_title(ctx, title);
    snprintf(s_buffer, BUFFER_LEN, "%d.%d", steps / 1000, (steps % 1000) / 100);
  } else {
    snprintf(s_buffer, BUFFER_LEN, "%s", "Steps");
    draw_title(ctx, title);
    snprintf(s_buffer, BUFFER_LEN, "%d", steps);
  }
  draw_value(ctx, value);
}

static void draw_temp(GContext* ctx, GRect value, GRect title) {
  snprintf(s_buffer, BUFFER_LEN, "%s", "Weather");
  draw_title(ctx, title);
  if (s_weather.temp_deci_c == INVALID_TEMP) {
    snprintf(s_buffer, BUFFER_LEN, "%s°", "--");
  } else if (s_settings.temperature_in_celsius) {
    if (s_settings.temperature_tenths) {
      snprintf(s_buffer, BUFFER_LEN, "%d.%d°c", s_weather.temp_deci_c / 10, s_weather.temp_deci_c % 10);
    } else {
      snprintf(s_buffer, BUFFER_LEN, "%d°c", (s_weather.temp_deci_c + 5) / 10);
    }
  } else {
    int temp_deci_f = s_weather.temp_deci_c * 9 / 5 + 320;
    if (s_settings.temperature_tenths) {
      snprintf(s_buffer, BUFFER_LEN, "%d.%d°f", temp_deci_f / 10, temp_deci_f % 10);
    } else {
      snprintf(s_buffer, BUFFER_LEN, "%d°f", (temp_deci_f + 5) / 10);
    }
  }
  draw_value(ctx, value);
}

static void draw_heart_rate(GContext* ctx, GRect value, GRect title) {
  snprintf(s_buffer, BUFFER_LEN, "%s", "Heart");
  draw_title(ctx, title);
  int bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (bpm > 0) {
    snprintf(s_buffer, BUFFER_LEN, "%d", bpm);
  } else {
    snprintf(s_buffer, BUFFER_LEN, "%s", "--");
  }
  draw_value(ctx, value);
}

static void draw_rain(GContext* ctx, GRect value, GRect title, int hours, int rain_dmm) {
  snprintf(s_buffer, BUFFER_LEN, "Rain %dh", hours);
  draw_title(ctx, title);
  if (rain_dmm == INVALID_RAIN) {
    snprintf(s_buffer, BUFFER_LEN, "%s", "--");
  } else {
    snprintf(s_buffer, BUFFER_LEN, "%d.%d", rain_dmm / 10, rain_dmm % 10);
  }
  draw_value(ctx, value);
}

static void draw_module_time(GContext* ctx, GRect value, GRect title, const char* desc, time_t time_secs) {
  snprintf(s_buffer, BUFFER_LEN, "%s", desc);
  draw_title(ctx, title);
  if (time_secs == INVALID_TIME) {
    snprintf(s_buffer, BUFFER_LEN, "%s", "--");
  } else {
    struct tm* time_struct = localtime(&time_secs);
    if (clock_is_24h_style()) {
      strftime(s_buffer, BUFFER_LEN, "%H:%M", time_struct);
    } else {
      strftime(s_buffer, BUFFER_LEN, "%l:%M", time_struct);
    }
  }
  draw_value(ctx, value);
}

static void draw_watch_status(GContext* ctx, GRect value, GRect title) {
  snprintf(s_buffer, BUFFER_LEN, "%s", "Status");
  draw_title(ctx, title);
  char q = quiet_time_is_active() ? 'Q' : 'q';
  char b = connection_service_peek_pebble_app_connection() ? 'B' : 'b';
  snprintf(s_buffer, BUFFER_LEN, "%c %c", q, b);
  draw_value(ctx, value);
}

static void draw_error_codes(GContext* ctx, GRect value, GRect title) {
  snprintf(s_buffer, BUFFER_LEN, "%s", "Error");
  draw_title(ctx, title);
  graphics_context_set_text_color(ctx, s_settings.color_corner_value);
  snprintf(s_buffer, BUFFER_LEN, "%d %d\n%d %d", s_error_codes[0], s_error_codes[1], s_error_codes[2], s_error_codes[3]);
  draw_text(ctx, s_buffer, s_font_sm, value, GTextAlignmentCenter, 0);
}

static void draw_module(GContext* ctx, uint8_t module_id, struct tm* now, GRect full_bbox, bool title_on_top, bool title_hide) {
  GRect value, title;
  hsplit_rect(ctx, full_bbox, &value, &title, title_on_top, title_hide);
  if (module_id == MODULE_BATTERY) {
    draw_batt(ctx, value, title);
  } else if (module_id == MODULE_DATE) {
    draw_date(ctx, value, title, now);
  } else if (module_id == MODULE_STEPS) {
    draw_steps(ctx, value, title);
  } else if (module_id == MODULE_TEMP_NOW) {
    draw_temp(ctx, value, title);
  } else if (module_id == MODULE_HEART_RATE) {
    draw_heart_rate(ctx, value, title);
  } else if (module_id == MODULE_RAIN_1H) {
    draw_rain(ctx, value, title, 1, s_weather.rain_1h_dmm);
  } else if (module_id == MODULE_RAIN_6H) {
    draw_rain(ctx, value, title, 6, s_weather.rain_6h_dmm);
  } else if (module_id == MODULE_SUNRISE) {
    draw_module_time(ctx, value, title, "Sunrise", s_weather.sunrise);
  } else if (module_id == MODULE_SUNSET) {
    draw_module_time(ctx, value, title, "Sunset", s_weather.sunset);
  } else if (module_id == MODULE_WATCH_STATUS) {
    draw_watch_status(ctx, value, title);
  } else if (module_id == MODULE_ERROR_CODES) {
    draw_error_codes(ctx, value, title);
  }
  draw_separator(ctx, full_bbox, title_on_top);
}

static void maybe_request_weather() {
  time_t now = time(NULL);
  bool should_request = false;
  if (s_weather.temp_deci_c == INVALID_TEMP && now >= s_last_request_sent + s_weather_retry_seconds) {
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

static void on_wake() {
  if (s_settings.include_seconds == SECONDS_ON_WAKE) {
    s_last_wake = time(NULL);
    s_time_units = SECOND_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
    if (s_layer) layer_mark_dirty(s_layer);
  }
}

static void handle_accel_tap(AccelAxisType axis, int32_t direction) {
  on_wake();
}

static void handle_backlight(bool on) {
  if (on) on_wake();
}

static void tick_resub(time_t now_s) {
  if (s_settings.include_seconds == SECONDS_NEVER && s_time_units != MINUTE_UNIT) {
    s_time_units = MINUTE_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  } else if (s_settings.include_seconds == SECONDS_ON_WAKE && now_s > s_last_wake + 10 && s_time_units != MINUTE_UNIT) {
    s_time_units = MINUTE_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  } else if (s_settings.include_seconds == SECONDS_ALWAYS && s_time_units != SECOND_UNIT) {
    s_time_units = SECOND_UNIT;
    tick_timer_service_subscribe(s_time_units, tick_handler);
  }
}

static void update_layer(Layer* layer, GContext* ctx) {
  time_t now_s = time(NULL);
  struct tm* now = localtime(&now_s);
  tick_resub(now_s);
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
  graphics_fill_rect(ctx, visible, 0, GCornerNone);

  int time_bbox_height = draw_time(ctx, now, visible);

  int complication_height = (visible.size.h - time_bbox_height) / 2 - 10;
  GSize complication_size = GSize(visible.size.w / 2, complication_height);
  int left  = visible.origin.x + visible.size.w / 4;
  int right = visible.origin.x + visible.size.w * 3 / 4;
  int top = visible.origin.y                 + complication_height / 2;
  int bot = visible.origin.y + visible.size.h - complication_height / 2;
  bool title_hide = timeline_quick_view;

  draw_module(ctx, s_settings.module_top_left, now, rect_from_center(GPoint(left,  top), complication_size), true, title_hide);
  draw_module(ctx, s_settings.module_top_right, now, rect_from_center(GPoint(right, top), complication_size), true, title_hide);
  draw_module(ctx, s_settings.module_bot_left, now, rect_from_center(GPoint(left,  bot), complication_size), false, title_hide);
  draw_module(ctx, s_settings.module_bot_right, now, rect_from_center(GPoint(right, bot), complication_size), false, title_hide);
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
  if (loaded_version < 3) {
    s_settings.module_top_left = DEFAULT_MODULE_TOP_LEFT;
    s_settings.module_top_right = DEFAULT_MODULE_TOP_RIGHT;
    s_settings.module_bot_left = DEFAULT_MODULE_BOT_LEFT;
    s_settings.module_bot_right = DEFAULT_MODULE_BOT_RIGHT;
  }
}

static void save_settings() {
  persist_write_int(SETTINGS_VERSION_KEY, 3);
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(ClaySettings));
}

static void save_error_code(int ec) {
  s_error_codes[s_error_code_idx] = ec;
  s_error_code_idx = (s_error_code_idx + 1) % NUM_ERROR_CODES;
}

static void inbox_dropped_handler(AppMessageResult reason, void* context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Dropped because %d", reason);
  // Error code scheme:
  //   0 is a null error code
  //   1-49 are from index.js
  //   50-99 are from dashboard.c
  //   100+ are from external servers (HTTP response codes)
  save_error_code(50);
}

static void inbox_received_handler(DictionaryIterator *iter, void* context) {
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_error_code))) {
    save_error_code(t->value->int32);
  }

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
  if ((t = dict_find(iter, MESSAGE_KEY_module_top_left              ))) { s_settings.module_top_left           = atoi(t->value->cstring); }
  if ((t = dict_find(iter, MESSAGE_KEY_module_top_right             ))) { s_settings.module_top_right          = atoi(t->value->cstring); }
  if ((t = dict_find(iter, MESSAGE_KEY_module_bot_left              ))) { s_settings.module_bot_left           = atoi(t->value->cstring); }
  if ((t = dict_find(iter, MESSAGE_KEY_module_bot_right             ))) { s_settings.module_bot_right          = atoi(t->value->cstring); }

  if ((t = dict_find(iter, MESSAGE_KEY_weather_rain_1h_dmm             ))) { s_weather.rain_1h_dmm  = t->value->int32; }
  if ((t = dict_find(iter, MESSAGE_KEY_weather_rain_6h_dmm             ))) { s_weather.rain_6h_dmm  = t->value->int32; }
  if ((t = dict_find(iter, MESSAGE_KEY_sunrise                         ))) { s_weather.sunrise = t->value->int32; }
  if ((t = dict_find(iter, MESSAGE_KEY_sunset                          ))) { s_weather.sunset = t->value->int32; }
  if ((t = dict_find(iter, MESSAGE_KEY_weather_now_temp_deci_c))) {
    s_weather.temp_deci_c = t->value->int32;
    s_weather_retry_seconds = INIT_WEATHER_RETRY_SECONDS;
  }

  save_settings();
  // Update the display based on new settings
  if (s_layer) { layer_mark_dirty(s_layer); }
}

static void init(void) {
  s_font_xl = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_68));
  s_font_lg = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_50));
  s_font_md = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_34));
  s_font_sm = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_22));

  load_settings();

  time_t now_s = time(NULL);

  s_weather.temp_deci_c = INVALID_TEMP;
  s_weather.rain_1h_dmm = INVALID_RAIN;
  s_weather.rain_6h_dmm = INVALID_RAIN;
  s_weather.sunrise = INVALID_TIME;
  s_weather.sunset = INVALID_TIME;

  s_last_request_sent = now_s;  // because js sends weather on "ready" event
  s_last_wake = now_s;  // Don't want the first update to compare against time 0.

  tick_timer_service_subscribe(s_time_units, tick_handler);
  accel_tap_service_subscribe(handle_accel_tap);
  backlight_service_subscribe(handle_backlight);
  on_wake(); // For SECONDS_ON_WAKE, loading the watch counts as a wakeup

  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(2048, 64);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  backlight_service_unsubscribe();
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
