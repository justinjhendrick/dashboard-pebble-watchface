#pragma once
#include <pebble.h>

static GRect rect_from_center(GPoint center, GSize size) {
  GRect ret;
  ret.origin.x = center.x - size.w / 2;
  ret.origin.y = center.y - size.h / 2;
  ret.size = size;
  return ret;
}

static int min(int a, int b) {
  if (a < b) {
    return a;
  }
  return b;
}

static int max(int a, int b) {
  if (a > b) {
    return a;
  }
  return b;
}

static void format_time(struct tm* now, bool include_seconds, char* buf, int buf_len) {
  if (clock_is_24h_style()) {
    if (include_seconds) {
      strftime(buf, buf_len, "%H:%M:%S", now);
    } else {
      strftime(buf, buf_len, "%H:%M", now);
    }
  } else {
    if (include_seconds) {
      strftime(buf, buf_len, "%I:%M:%S", now);
    } else {
      strftime(buf, buf_len, "%I:%M", now);
    }
  }
}

static void format_date(struct tm* now, bool month_first, char* buf, int buf_len) {
  int first = 0;
  int second = 0;
  if (month_first) {
    first = now->tm_mon + 1;
    second = now->tm_mday;
  } else {
    first = now->tm_mday;
    second = now->tm_mon + 1;
  }
  snprintf(buf, buf_len, "%d/%d", first, second);
}

static void draw_text_valign(GContext* ctx, const char* buffer, GRect bbox, GTextAlignment align, bool bold, int valign) {
  int h = bbox.size.h;
  int font_height = 0;
  int top_pad = 0;
  GFont font;
  if (h < 14) {
    return;
  } else if (h < 18) {
    font_height = 9;
    top_pad = 4;
    if (bold) {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    } else {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    }
  } else if (h < 24) {
    font_height = 11;
    top_pad = 6;
    if (bold) {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    } else {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    }
  } else if (h < 28) {
    font_height = 14;
    top_pad = 9;
    if (bold) {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    } else {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_24);
    }
  } else {
    font_height = 18;
    top_pad = 9;
    if (bold) {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    } else {
      font = fonts_get_system_font(FONT_KEY_GOTHIC_28);
    }
  }
  int bot_pad = h - font_height - top_pad;
  int shift_up = (top_pad - bot_pad) / 2 + 1;
  if (valign == 1) {
    shift_up = top_pad;
  } else if (valign == 2) {
    shift_up = -bot_pad;
  } else if (valign == 3) {
    shift_up = 0;
  }
  GRect fixed_bbox = GRect(bbox.origin.x, bbox.origin.y - shift_up, bbox.size.w, bbox.size.h);
  graphics_draw_text(ctx, buffer, font, fixed_bbox, GTextOverflowModeWordWrap, align, NULL);
}

static void draw_text_midalign(GContext* ctx, const char* buffer, GRect bbox, GTextAlignment align, bool bold) {
  draw_text_valign(ctx, buffer, bbox, align, bold, 0);
}

static void draw_text_topalign(GContext* ctx, const char* buffer, GRect bbox, GTextAlignment align, bool bold) {
  draw_text_valign(ctx, buffer, bbox, align, bold, 1);
}

static void draw_text_botalign(GContext* ctx, const char* buffer, GRect bbox, GTextAlignment align, bool bold) {
  draw_text_valign(ctx, buffer, bbox, align, bold, 2);
}

static void draw_text_noalign(GContext* ctx, const char* buffer, GRect bbox, GTextAlignment align, bool bold) {
  draw_text_valign(ctx, buffer, bbox, align, bold, 3);
}
