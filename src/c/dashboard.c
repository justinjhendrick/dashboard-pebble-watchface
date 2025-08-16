#include <pebble.h>

static Window* s_window;
static Layer* s_layer;
#define BUFFER_LEN (20)
static char s_buffer[BUFFER_LEN];

#define SETTINGS_VERSION_KEY (1)
#define SETTINGS_KEY (2)

typedef struct ClaySettings {
  GColor color_background;
  bool include_seconds;
} __attribute__((__packed__)) ClaySettings;

ClaySettings settings;

static void default_settings() {
  settings.color_background = GColorOxfordBlue;
  settings.include_seconds = true;
}

static void update_layer(Layer* layer, GContext* ctx) {
  time_t temp = time(NULL);
  struct tm* now = localtime(&temp);

  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, settings.color_background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
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
