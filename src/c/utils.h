#pragma once
#include <pebble.h>

static GRect rect_from_center(GPoint center, GSize size) {
  GRect ret;
  ret.origin.x = center.x - size.w / 2;
  ret.origin.y = center.y - size.h / 2;
  ret.size = size;
  return ret;
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
  if (month_first) {
    strftime(buf, buf_len, "%m/%d", now);
  } else {
    strftime(buf, buf_len, "%d/%m", now);
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
