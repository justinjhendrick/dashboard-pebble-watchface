#pragma once
#include <pebble.h>

static GRect rect_from_center(GPoint center, GSize size) {
  GRect ret;
  ret.origin.x = center.x - size.w / 2;
  ret.origin.y = center.y - size.h / 2;
  ret.size = size;
  return ret;
}

static int get_12h_hour(struct tm* now) {
  int hour = now->tm_hour % 12;
  return (hour == 0) ? 12 : hour;
}

static void format_time(
  struct tm* now,
  bool leading_zero_hour_in_12h,
  char* buf,
  int buf_len
) {
  if (clock_is_24h_style()) {
    strftime(buf, buf_len, "%H:%M", now);
  } else {
    if (leading_zero_hour_in_12h) {
      strftime(buf, buf_len, "%I:%M", now);
    } else {
      snprintf(buf, buf_len, "%d", get_12h_hour(now));
      size_t len = strlen(buf);
      strftime(buf + len, buf_len - len, ":%M", now);
    }
  }
}

static void format_date(struct tm* now, bool month_first, bool leading_zero, char* buf, int buf_len) {
  if (!leading_zero) {
    if (month_first) {
      snprintf(buf, buf_len, "%d/%d", now->tm_mon + 1, now->tm_mday);
    } else {
      snprintf(buf, buf_len, "%d/%d", now->tm_mday, now->tm_mon + 1);
    }
  } else {
    if (month_first) {
      strftime(buf, buf_len, "%m/%d", now);
    } else {
      strftime(buf, buf_len, "%d/%m", now);
    }
  }
}

static void draw_text(
    GContext* ctx,
    const char* buffer,
    GFont font,
    GRect bbox,
    GTextAlignment align,
    int shift_up
  ) {
  GRect fixed_bbox = GRect(bbox.origin.x, bbox.origin.y - shift_up, bbox.size.w, bbox.size.h);
  graphics_draw_text(ctx, buffer, font, fixed_bbox, GTextOverflowModeFill, align, NULL);
}
