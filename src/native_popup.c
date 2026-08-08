#include "native_popup.h"

#include "app_launcher.h"

#ifdef HAVE_NATIVE_PANEL

#include <cairo/cairo-xcb.h>
#include <ctype.h>
#include <glib.h>
#include <locale.h>
#include <pango/pangocairo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_XKBCOMMON_X11
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-x11.h>
#endif

#define POPUP_WIDTH 560
#define POPUP_VISIBLE_ROWS 12
#define FORECAST_WIDTH 700
#define FORECAST_TITLE_HEIGHT 28
#define FORECAST_DAY_HEADER_HEIGHT 26
#define FORECAST_PERIOD_HEIGHT 25
#define FORECAST_WEATHER_HEIGHT 43
#define FORECAST_VALUE_HEIGHT 24
#define FORECAST_DAY_HEIGHT                                                    \
  (FORECAST_DAY_HEADER_HEIGHT + FORECAST_PERIOD_HEIGHT +                       \
   FORECAST_WEATHER_HEIGHT + 3 * FORECAST_VALUE_HEIGHT)

typedef enum {
  POPUP_MODE_MENU,
  POPUP_MODE_INFO,
  POPUP_MODE_FORECAST,
  POPUP_MODE_AGENDA,
} PopupMode;

struct NativePopup {
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t window;
  cairo_surface_t *surface;
  cairo_t *cairo;
  PangoLayout *layout;
  PangoFontDescription *font;
  PangoFontDescription *iconFont;
  PanelConfig config;
  PopupItem items[POPUP_ITEM_MAX];
  size_t itemCount;
  size_t matches[POPUP_ITEM_MAX];
  int scores[POPUP_ITEM_MAX];
  size_t matchCount;
  size_t selected;
  size_t top;
  int x;
  int y;
  int width;
  int height;
  int rowHeight;
  int anchorX;
  int anchorY;
  int anchorWidth;
  int panelHeight;
  int forecastAnchorX;
  int forecastAnchorWidth;
  int agendaAnchorX;
  int agendaAnchorWidth;
  int agendaHover;
  xcb_window_t previousFocus;
  uint8_t previousRevertTo;
  bool focusSaved;
  bool open;
  bool pointerGrabbed;
  bool searchable;
  bool hasIconFont;
  PopupMode mode;
  char query[256];
  WeatherForecast forecast;
  AgendaView agenda;
  char forecastLocation[128];
#ifdef HAVE_XKBCOMMON_X11
  struct xkb_context *xkbContext;
  struct xkb_keymap *keymap;
  struct xkb_state *xkbState;
  struct xkb_compose_table *composeTable;
  struct xkb_compose_state *composeState;
#endif
};

static bool
parseColor(const char *value, double *red, double *green, double *blue) {
  unsigned rgb;
  if (!value || value[0] != '#' || strlen(value) != 7)
    return false;
  for (size_t i = 1; i < 7; i++)
    if (!isxdigit((unsigned char)value[i]))
      return false;
  if (sscanf(value + 1, "%06x", &rgb) != 1)
    return false;
  *red = (double)((rgb >> 16U) & 0xffU) / 255.0;
  *green = (double)((rgb >> 8U) & 0xffU) / 255.0;
  *blue = (double)(rgb & 0xffU) / 255.0;
  return true;
}

static void setColor(cairo_t *cairo, const char *value, const char *fallback) {
  double red = 0.0, green = 0.0, blue = 0.0;
  if (!parseColor(value, &red, &green, &blue))
    parseColor(fallback, &red, &green, &blue);
  cairo_set_source_rgb(cairo, red, green, blue);
}

static void setColorAlpha(cairo_t *cairo,
                          const char *value,
                          const char *fallback,
                          double alpha) {
  double red = 0.0, green = 0.0, blue = 0.0;
  if (!parseColor(value, &red, &green, &blue))
    parseColor(fallback, &red, &green, &blue);
  cairo_set_source_rgba(cairo, red, green, blue, alpha);
}

static void fontDescription(const char *configured, char *output, size_t size) {
  char copy[128];
  snprintf(copy, sizeof(copy), "%s", configured);
  char *style = strstr(copy, ":style=");
  char *fontSize = strstr(copy, ":size=");
  if (style)
    *style = '\0';
  else if (fontSize)
    *fontSize = '\0';
  const char *styleValue = style ? style + 7 : "Regular";
  if (fontSize && style && fontSize > style)
    *fontSize = '\0';
  const char *sizeValue = fontSize ? fontSize + 6 : "10";
  snprintf(output, size, "%s %s %s", copy, styleValue, sizeValue);
}

static bool fontAvailable(PangoLayout *layout,
                          const PangoFontDescription *requested) {
  PangoContext *context = pango_layout_get_context(layout);
  PangoFont *font = pango_context_load_font(context, requested);
  if (!font)
    return false;
  PangoFontDescription *actual = pango_font_describe(font);
  const char *requestedFamily = pango_font_description_get_family(requested);
  const char *actualFamily = pango_font_description_get_family(actual);
  bool available = requestedFamily && actualFamily &&
                   g_ascii_strcasecmp(requestedFamily, actualFamily) == 0;
  pango_font_description_free(actual);
  g_object_unref(font);
  return available;
}

static xcb_visualtype_t *findVisual(xcb_screen_t *screen) {
  xcb_depth_iterator_t depths = xcb_screen_allowed_depths_iterator(screen);
  for (; depths.rem; xcb_depth_next(&depths)) {
    xcb_visualtype_iterator_t visuals = xcb_depth_visuals_iterator(depths.data);
    for (; visuals.rem; xcb_visualtype_next(&visuals))
      if (visuals.data->visual_id == screen->root_visual)
        return visuals.data;
  }
  return NULL;
}

static bool shouldSwap(const NativePopup *popup, size_t left, size_t right) {
  if (popup->scores[left] != popup->scores[right])
    return popup->scores[left] > popup->scores[right];
  int collated = g_utf8_collate(popup->items[popup->matches[left]].label,
                                popup->items[popup->matches[right]].label);
  return collated > 0 ||
         (collated == 0 && popup->matches[left] > popup->matches[right]);
}

static void updateMatches(NativePopup *popup) {
  popup->matchCount = 0;
  for (size_t i = 0; i < popup->itemCount; i++) {
    int score = appSearchRank(
        popup->items[i].label, popup->items[i].search, popup->query);
    if (score >= 0) {
      popup->matches[popup->matchCount] = i;
      popup->scores[popup->matchCount++] = score;
    }
  }
  for (size_t i = 1; i < popup->matchCount; i++) {
    size_t j = i;
    while (j > 0 && shouldSwap(popup, j - 1, j)) {
      size_t index = popup->matches[j - 1];
      popup->matches[j - 1] = popup->matches[j];
      popup->matches[j] = index;
      int score = popup->scores[j - 1];
      popup->scores[j - 1] = popup->scores[j];
      popup->scores[j] = score;
      j--;
    }
  }
  popup->selected = 0;
  popup->top = 0;
}

static bool iconCodepoint(gunichar codepoint) {
  return (codepoint >= 0xe000U && codepoint <= 0xf8ffU) ||
         (codepoint >= 0xf0000U && codepoint <= 0xffffdU) ||
         (codepoint >= 0x100000U && codepoint <= 0x10fffdU);
}

static void prepareLayout(NativePopup *popup, const char *text) {
  pango_layout_set_text(popup->layout, text, -1);
  PangoAttrList *attributes = pango_attr_list_new();
  const char *cursor = text;
  while (*cursor) {
    const char *next = g_utf8_next_char(cursor);
    if (iconCodepoint(g_utf8_get_char(cursor))) {
      PangoAttribute *attribute = pango_attr_font_desc_new(popup->iconFont);
      attribute->start_index = (guint)(cursor - text);
      attribute->end_index = (guint)(next - text);
      pango_attr_list_insert(attributes, attribute);
    }
    cursor = next;
  }
  pango_layout_set_attributes(popup->layout, attributes);
  pango_attr_list_unref(attributes);
}

static void drawText(
    NativePopup *popup, const char *text, int x, int y, const char *color) {
  setColor(popup->cairo, color, "#ffffff");
  prepareLayout(popup, text);
  cairo_move_to(popup->cairo, x, y);
  pango_cairo_show_layout(popup->cairo, popup->layout);
}

static void drawCenteredText(NativePopup *popup,
                             const char *text,
                             int x,
                             int y,
                             int width,
                             int height,
                             const char *color) {
  setColor(popup->cairo, color, "#ffffff");
  prepareLayout(popup, text);
  int textWidth = 0, textHeight = 0;
  pango_layout_get_pixel_size(popup->layout, &textWidth, &textHeight);
  cairo_move_to(popup->cairo,
                x + (width - textWidth) / 2.0,
                y + (height - textHeight) / 2.0);
  pango_cairo_show_layout(popup->cairo, popup->layout);
}

static int textWidth(NativePopup *popup, const char *text) {
  prepareLayout(popup, text);
  int width = 0;
  pango_layout_get_pixel_size(popup->layout, &width, NULL);
  return width;
}

static void drawRightAlignedText(
    NativePopup *popup, const char *text, int right, int y, const char *color) {
  drawText(popup, text, right - textWidth(popup, text), y, color);
}

static void drawEllipsizedText(NativePopup *popup,
                               const char *text,
                               int x,
                               int y,
                               int width,
                               const char *color) {
  setColor(popup->cairo, color, "#ffffff");
  prepareLayout(popup, text);
  pango_layout_set_width(popup->layout, width * PANGO_SCALE);
  pango_layout_set_ellipsize(popup->layout, PANGO_ELLIPSIZE_END);
  cairo_move_to(popup->cairo, x, y);
  pango_cairo_show_layout(popup->cairo, popup->layout);
  pango_layout_set_width(popup->layout, -1);
  pango_layout_set_ellipsize(popup->layout, PANGO_ELLIPSIZE_NONE);
}

static int wrappedTextHeight(NativePopup *popup, const char *text, int width) {
  prepareLayout(popup, text);
  pango_layout_set_width(popup->layout, width * PANGO_SCALE);
  pango_layout_set_wrap(popup->layout, PANGO_WRAP_WORD_CHAR);
  int height = 0;
  pango_layout_get_pixel_size(popup->layout, NULL, &height);
  pango_layout_set_width(popup->layout, -1);
  return height;
}

static void drawWrappedText(NativePopup *popup,
                            const char *text,
                            int x,
                            int y,
                            int width,
                            const char *color) {
  setColor(popup->cairo, color, "#ffffff");
  prepareLayout(popup, text);
  pango_layout_set_width(popup->layout, width * PANGO_SCALE);
  pango_layout_set_wrap(popup->layout, PANGO_WRAP_WORD_CHAR);
  cairo_move_to(popup->cairo, x, y);
  pango_cairo_show_layout(popup->cairo, popup->layout);
  pango_layout_set_width(popup->layout, -1);
}

static void drawMenu(NativePopup *popup) {
  setColor(popup->cairo, popup->config.colorPanelBg, "#000000");
  cairo_paint(popup->cairo);
  int y = 0;
  if (popup->searchable) {
    setColor(popup->cairo, popup->config.colorBg, "#222222");
    cairo_rectangle(popup->cairo, 0, 0, popup->width, popup->rowHeight);
    cairo_fill(popup->cairo);
    char prompt[320];
    snprintf(prompt,
             sizeof(prompt),
             "%s: %s",
             panelLanguageIsGerman(&popup->config) ? "Suche" : "Search",
             popup->query);
    drawText(popup, prompt, 10, 5, popup->config.colorClock);
    y += popup->rowHeight;
  }
  size_t visible = popup->matchCount - popup->top;
  if (visible > POPUP_VISIBLE_ROWS)
    visible = POPUP_VISIBLE_ROWS;
  for (size_t row = 0; row < visible; row++) {
    size_t position = popup->top + row;
    if (position == popup->selected) {
      setColor(popup->cairo, popup->config.colorFocusedFreeBg, "#333333");
      cairo_rectangle(popup->cairo,
                      0,
                      y + (int)row * popup->rowHeight,
                      popup->width,
                      popup->rowHeight);
      cairo_fill(popup->cairo);
    }
    drawText(popup,
             popup->items[popup->matches[position]].label,
             10,
             y + (int)row * popup->rowHeight + 5,
             position == popup->selected ? popup->config.colorFocus
                                         : popup->config.colorFree);
  }
  if (popup->matchCount == 0)
    drawText(popup,
             panelLanguageIsGerman(&popup->config) ? "Keine Treffer"
                                                   : "No matches",
             10,
             y + 5,
             popup->config.colorMuted);
}

static void
formatTemperatureRange(bool valid, int temperature, char *output, size_t size) {
  if (valid)
    snprintf(output, size, "%d°", temperature);
  else
    snprintf(output, size, "%s", "–");
}

static void
formatTemperatureValue(bool valid, int temperature, char *output, size_t size) {
  if (valid)
    snprintf(output, size, "%d°C", temperature);
  else
    snprintf(output, size, "%s", "–");
}

static void formatDayTitle(const NativePopup *popup,
                           const WeatherForecastDay *day,
                           time_t now,
                           char *output,
                           size_t size) {
  char weekday[32], minimum[16], maximum[16];
  weatherForecastDayLabel(day->date,
                          panelLanguageIsGerman(&popup->config),
                          now,
                          weekday,
                          sizeof(weekday));
  formatTemperatureRange(
      day->minimumValid, day->minimumC, minimum, sizeof(minimum));
  formatTemperatureRange(
      day->maximumValid, day->maximumC, maximum, sizeof(maximum));
  snprintf(output, size, "%s · %s–%s", weekday, minimum, maximum);
}

static void configureIconStroke(NativePopup *popup, const char *color) {
  setColor(popup->cairo, color, "#ffffff");
  cairo_set_line_width(popup->cairo, 1.6);
  cairo_set_line_cap(popup->cairo, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(popup->cairo, CAIRO_LINE_JOIN_ROUND);
}

static void drawSun(NativePopup *popup, double x, double y, double radius) {
  static const double RAY_X[] = {
      1.0, 0.707, 0.0, -0.707, -1.0, -0.707, 0.0, 0.707};
  static const double RAY_Y[] = {
      0.0, 0.707, 1.0, 0.707, 0.0, -0.707, -1.0, -0.707};
  configureIconStroke(popup, popup->config.colorWeather);
  cairo_new_sub_path(popup->cairo);
  cairo_arc(popup->cairo, x, y, radius, 0.0, 2.0 * G_PI);
  cairo_stroke(popup->cairo);
  for (int ray = 0; ray < 8; ray++) {
    cairo_move_to(popup->cairo,
                  x + RAY_X[ray] * (radius + 3.0),
                  y + RAY_Y[ray] * (radius + 3.0));
    cairo_line_to(popup->cairo,
                  x + RAY_X[ray] * (radius + 6.0),
                  y + RAY_Y[ray] * (radius + 6.0));
  }
  cairo_stroke(popup->cairo);
}

static void drawCloud(NativePopup *popup, double x, double y) {
  configureIconStroke(popup, popup->config.colorFree);
  cairo_move_to(popup->cairo, x - 15.0, y + 7.0);
  cairo_curve_to(
      popup->cairo, x - 19.0, y + 7.0, x - 20.0, y + 1.0, x - 16.0, y - 2.0);
  cairo_curve_to(
      popup->cairo, x - 14.0, y - 4.0, x - 12.0, y - 4.0, x - 9.0, y - 3.0);
  cairo_curve_to(
      popup->cairo, x - 7.0, y - 10.0, x + 3.0, y - 12.0, x + 8.0, y - 6.0);
  cairo_curve_to(
      popup->cairo, x + 15.0, y - 7.0, x + 19.0, y - 2.0, x + 18.0, y + 3.0);
  cairo_curve_to(
      popup->cairo, x + 22.0, y + 6.0, x + 19.0, y + 11.0, x + 15.0, y + 11.0);
  cairo_line_to(popup->cairo, x - 14.0, y + 11.0);
  cairo_curve_to(
      popup->cairo, x - 18.0, y + 11.0, x - 19.0, y + 8.0, x - 15.0, y + 7.0);
  cairo_close_path(popup->cairo);
  cairo_stroke(popup->cairo);
}

static void drawMoon(NativePopup *popup, double x, double y) {
  configureIconStroke(popup, popup->config.colorWeather);
  cairo_move_to(popup->cairo, x + 5.0, y - 11.0);
  cairo_curve_to(
      popup->cairo, x - 4.0, y - 8.0, x - 7.0, y + 3.0, x - 1.0, y + 10.0);
  cairo_curve_to(
      popup->cairo, x + 5.0, y + 16.0, x + 15.0, y + 12.0, x + 17.0, y + 4.0);
  cairo_curve_to(
      popup->cairo, x + 9.0, y + 8.0, x + 1.0, y + 1.0, x + 5.0, y - 11.0);
  cairo_close_path(popup->cairo);
  cairo_stroke(popup->cairo);
  cairo_move_to(popup->cairo, x + 15.0, y - 8.0);
  cairo_line_to(popup->cairo, x + 15.0, y - 2.0);
  cairo_move_to(popup->cairo, x + 12.0, y - 5.0);
  cairo_line_to(popup->cairo, x + 18.0, y - 5.0);
  cairo_stroke(popup->cairo);
}

static void drawRain(NativePopup *popup, double x, double y) {
  configureIconStroke(popup, popup->config.colorWeather);
  for (int drop = -1; drop <= 1; drop++) {
    double dropX = x + (double)drop * 9.0;
    cairo_move_to(popup->cairo, dropX + 2.0, y + 10.0);
    cairo_line_to(popup->cairo, dropX, y + 16.0);
  }
  cairo_stroke(popup->cairo);
}

static void drawSnow(NativePopup *popup, double x, double y) {
  configureIconStroke(popup, popup->config.colorWeather);
  for (int flake = -1; flake <= 1; flake++) {
    double flakeX = x + (double)flake * 10.0;
    cairo_move_to(popup->cairo, flakeX - 2.0, y + 13.0);
    cairo_line_to(popup->cairo, flakeX + 2.0, y + 17.0);
    cairo_move_to(popup->cairo, flakeX + 2.0, y + 13.0);
    cairo_line_to(popup->cairo, flakeX - 2.0, y + 17.0);
  }
  cairo_stroke(popup->cairo);
}

static void drawFog(NativePopup *popup, double x, double y) {
  configureIconStroke(popup, popup->config.colorWeather);
  for (int line = -1; line <= 1; line++) {
    double lineY = y + (double)line * 6.0;
    cairo_move_to(popup->cairo, x - 17.0 + (line == 0 ? 4.0 : 0.0), lineY);
    cairo_line_to(popup->cairo, x + 17.0 - (line == 0 ? 0.0 : 4.0), lineY);
  }
  cairo_stroke(popup->cairo);
}

static void drawThunder(NativePopup *popup, double x, double y) {
  configureIconStroke(popup, popup->config.colorSystem);
  cairo_move_to(popup->cairo, x + 2.0, y + 8.0);
  cairo_line_to(popup->cairo, x - 3.0, y + 16.0);
  cairo_line_to(popup->cairo, x + 2.0, y + 15.0);
  cairo_line_to(popup->cairo, x - 1.0, y + 21.0);
  cairo_line_to(popup->cairo, x + 8.0, y + 11.0);
  cairo_line_to(popup->cairo, x + 3.0, y + 12.0);
  cairo_stroke(popup->cairo);
}

static void drawWeatherIcon(NativePopup *popup,
                            WeatherCondition condition,
                            int hour,
                            int x,
                            int y,
                            int width,
                            int height) {
  double centerX = (double)x + (double)width / 2.0;
  double centerY = (double)y + (double)height / 2.0 - 1.0;
  bool night = hour >= 20 || hour < 6;
  cairo_save(popup->cairo);
  switch (condition) {
  case WEATHER_CONDITION_CLEAR:
    if (night)
      drawMoon(popup, centerX - 2.0, centerY - 1.0);
    else
      drawSun(popup, centerX, centerY, 8.0);
    break;
  case WEATHER_CONDITION_PARTLY_CLOUDY:
    if (night)
      drawMoon(popup, centerX + 5.0, centerY - 6.0);
    else
      drawSun(popup, centerX + 8.0, centerY - 7.0, 6.0);
    drawCloud(popup, centerX - 4.0, centerY + 3.0);
    break;
  case WEATHER_CONDITION_CLOUDY:
    drawCloud(popup, centerX, centerY);
    break;
  case WEATHER_CONDITION_RAIN:
    drawCloud(popup, centerX, centerY - 5.0);
    drawRain(popup, centerX, centerY - 2.0);
    break;
  case WEATHER_CONDITION_THUNDER:
    drawCloud(popup, centerX, centerY - 5.0);
    drawThunder(popup, centerX, centerY - 3.0);
    break;
  case WEATHER_CONDITION_SNOW:
    drawCloud(popup, centerX, centerY - 5.0);
    drawSnow(popup, centerX, centerY - 3.0);
    break;
  case WEATHER_CONDITION_FOG:
    drawFog(popup, centerX, centerY);
    break;
  case WEATHER_CONDITION_UNKNOWN:
  default:
    drawCenteredText(popup, "?", x, y, width, height, popup->config.colorMuted);
    break;
  }
  cairo_restore(popup->cairo);
}

static void drawForecastGrid(NativePopup *popup,
                             int dayY,
                             int labelWidth,
                             int slotWidth,
                             int remainder) {
  int periodY = dayY + FORECAST_DAY_HEADER_HEIGHT;
  int weatherY = periodY + FORECAST_PERIOD_HEIGHT;
  int temperatureY = weatherY + FORECAST_WEATHER_HEIGHT;
  int rainY = temperatureY + FORECAST_VALUE_HEIGHT;
  int windY = rainY + FORECAST_VALUE_HEIGHT;
  int bottomY = windY + FORECAST_VALUE_HEIGHT;
  setColorAlpha(popup->cairo, popup->config.colorFree, "#bfbfbf", 0.38);
  cairo_set_line_width(popup->cairo, 1.0);
  const int HORIZONTAL[] = {
      dayY, periodY, weatherY, temperatureY, rainY, windY, bottomY};
  for (size_t line = 0; line < sizeof(HORIZONTAL) / sizeof(HORIZONTAL[0]);
       line++) {
    double lineY = (double)HORIZONTAL[line] + 0.5;
    cairo_move_to(popup->cairo, 0.5, lineY);
    cairo_line_to(popup->cairo, (double)popup->width - 0.5, lineY);
  }
  int gridX = labelWidth;
  cairo_move_to(popup->cairo, (double)gridX + 0.5, (double)periodY + 0.5);
  cairo_line_to(popup->cairo, (double)gridX + 0.5, (double)bottomY + 0.5);
  for (size_t slot = 0; slot < WEATHER_FORECAST_SLOT_COUNT; slot++) {
    gridX += slotWidth + ((int)slot < remainder ? 1 : 0);
    double lineX = slot + 1 == WEATHER_FORECAST_SLOT_COUNT
                       ? (double)popup->width - 0.5
                       : (double)gridX + 0.5;
    cairo_move_to(popup->cairo, lineX, (double)periodY + 0.5);
    cairo_line_to(popup->cairo, lineX, (double)bottomY + 0.5);
  }
  cairo_stroke(popup->cairo);
}

static void drawForecast(NativePopup *popup) {
  setColor(popup->cairo, popup->config.colorPanelBg, "#000000");
  cairo_paint(popup->cairo);
  bool german = panelLanguageIsGerman(&popup->config);
  const char *title = popup->forecastLocation[0]
                          ? popup->forecastLocation
                          : (german ? "Wettervorhersage" : "Weather forecast");
  char updated[64];
  time_t now = time(NULL);
  weatherForecastUpdatedLabel(popup->forecast.updatedAt,
                              popup->forecast.updatedAtValid,
                              german,
                              now,
                              updated,
                              sizeof(updated));
  int updatedWidth = textWidth(popup, updated);
  int updatedLeft = popup->width - 10 - updatedWidth;
  int titleWidth = updatedLeft - 20;
  if (titleWidth < 1)
    titleWidth = 1;
  drawEllipsizedText(
      popup, title, 10, 7, titleWidth, popup->config.colorWeather);
  drawRightAlignedText(
      popup, updated, popup->width - 10, 7, popup->config.colorFree);
  if (popup->forecast.dayCount == 0) {
    drawText(popup,
             german ? "Keine Vorhersagedaten verfügbar"
                    : "No forecast data available",
             10,
             FORECAST_TITLE_HEIGHT + 7,
             popup->config.colorMuted);
    return;
  }

  int labelWidth = popup->width >= 600 ? 144 : popup->width / 5;
  if (labelWidth < 72)
    labelWidth = 72;
  int contentWidth = popup->width - labelWidth;
  int slotWidth = contentWidth / WEATHER_FORECAST_SLOT_COUNT;
  int remainder = contentWidth % WEATHER_FORECAST_SLOT_COUNT;
  for (size_t dayIndex = 0; dayIndex < WEATHER_FORECAST_DAY_COUNT; dayIndex++) {
    const WeatherForecastDay *day = &popup->forecast.days[dayIndex];
    int dayY = FORECAST_TITLE_HEIGHT + (int)dayIndex * FORECAST_DAY_HEIGHT;
    setColor(popup->cairo, popup->config.colorBg, "#222222");
    cairo_rectangle(popup->cairo, 0, dayY, popup->width, FORECAST_DAY_HEIGHT);
    cairo_fill(popup->cairo);
    char dayTitle[96];
    if (day->available)
      formatDayTitle(popup, day, now, dayTitle, sizeof(dayTitle));
    else
      snprintf(dayTitle, sizeof(dayTitle), "%s", "–");
    drawForecastGrid(popup, dayY, labelWidth, slotWidth, remainder);
    drawText(popup, dayTitle, 10, dayY + 4, popup->config.colorWeather);
    int timeY = dayY + FORECAST_DAY_HEADER_HEIGHT;
    int weatherY = timeY + FORECAST_PERIOD_HEIGHT;
    int temperatureY = weatherY + FORECAST_WEATHER_HEIGHT;
    int rainY = temperatureY + FORECAST_VALUE_HEIGHT;
    int windY = rainY + FORECAST_VALUE_HEIGHT;
    static const char *const GERMAN_PERIODS[] = {
        "Morgens", "Mittags", "Abends", "Nachts"};
    static const char *const ENGLISH_PERIODS[] = {
        "Morning", "Noon", "Evening", "Night"};
    const char *const *periods = german ? GERMAN_PERIODS : ENGLISH_PERIODS;

    drawText(popup,
             german ? "Wetter" : "Weather",
             10,
             weatherY + 3,
             popup->config.colorFree);
    drawText(popup,
             german ? "Temperatur" : "Temperature",
             10,
             temperatureY + 3,
             popup->config.colorFree);
    drawText(popup,
             german ? "Regen" : "Rain",
             10,
             rainY + 3,
             popup->config.colorFree);
    drawText(popup, "Wind", 10, windY + 3, popup->config.colorFree);
    int slotX = labelWidth;
    for (size_t slotIndex = 0; slotIndex < WEATHER_FORECAST_SLOT_COUNT;
         slotIndex++) {
      int width = slotWidth + ((int)slotIndex < remainder ? 1 : 0);
      const WeatherForecastSlot *slot = &day->slots[slotIndex];
      char text[32];
      drawCenteredText(popup,
                       periods[slotIndex],
                       slotX,
                       timeY,
                       width,
                       FORECAST_PERIOD_HEIGHT,
                       popup->config.colorFree);
      if (slot->codeValid)
        drawWeatherIcon(popup,
                        slot->condition,
                        slot->hour,
                        slotX,
                        weatherY,
                        width,
                        FORECAST_WEATHER_HEIGHT);
      else
        drawCenteredText(popup,
                         "–",
                         slotX,
                         weatherY,
                         width,
                         FORECAST_WEATHER_HEIGHT,
                         popup->config.colorMuted);
      formatTemperatureValue(
          slot->temperatureValid, slot->temperatureC, text, sizeof(text));
      drawCenteredText(popup,
                       text,
                       slotX,
                       temperatureY,
                       width,
                       FORECAST_VALUE_HEIGHT,
                       popup->config.colorFree);
      if (slot->rainValid)
        snprintf(text, sizeof(text), "%d%%", slot->rainPercent);
      else
        snprintf(text, sizeof(text), "%s", "–");
      drawCenteredText(popup,
                       text,
                       slotX,
                       rainY,
                       width,
                       FORECAST_VALUE_HEIGHT,
                       popup->config.colorNetwork);
      if (slot->windDirectionValid && slot->windSpeedValid)
        snprintf(text,
                 sizeof(text),
                 "%s %d km/h",
                 weatherWindDirectionGlyph(slot->windDirection),
                 slot->windSpeedKmph);
      else if (slot->windSpeedValid)
        snprintf(text, sizeof(text), "%d km/h", slot->windSpeedKmph);
      else if (slot->windDirectionValid)
        snprintf(text,
                 sizeof(text),
                 "%s",
                 weatherWindDirectionGlyph(slot->windDirection));
      else
        snprintf(text, sizeof(text), "%s", "–");
      drawCenteredText(popup,
                       text,
                       slotX,
                       windY,
                       width,
                       FORECAST_VALUE_HEIGHT,
                       popup->config.colorSystem);
      slotX += width;
    }
  }
  setColorAlpha(popup->cairo, popup->config.colorFree, "#bfbfbf", 0.38);
  cairo_set_line_width(popup->cairo, 1.0);
  cairo_rectangle(popup->cairo,
                  0.5,
                  0.5,
                  (double)popup->width - 1.0,
                  (double)popup->height - 1.0);
  cairo_stroke(popup->cairo);
}

static void formatAgendaMetadata(const NativePopup *popup,
                                 const AgendaDisplayItem *display,
                                 char *output,
                                 size_t size) {
  bool german = panelLanguageIsGerman(&popup->config);
  const char *organizer = display->item.organizer[0]
                              ? display->item.organizer
                              : (german ? "nicht angegeben" : "not specified");
  snprintf(
      output, size, german ? "Organisator: %s" : "Organizer: %s", organizer);
  if (popup->config.agendaShowSource && display->item.sourceName[0]) {
    size_t used = strlen(output);
    snprintf(output + used,
             size > used ? size - used : 0,
             german ? " · Kalender: %s" : " · Calendar: %s",
             display->item.sourceName);
  }
}

static int agendaItemHeight(NativePopup *popup,
                            const AgendaDisplayItem *display) {
  if (display->item.type != AGENDA_ITEM_EVENT)
    return popup->rowHeight;
  const char *title =
      display->item.title[0]
          ? display->item.title
          : (panelLanguageIsGerman(&popup->config) ? "(Ohne Titel)"
                                                   : "(Untitled)");
  int whenWidth = textWidth(popup, display->when);
  int titleWidth = popup->width - (30 + whenWidth + 12) - 10;
  if (titleWidth < 1)
    titleWidth = 1;
  int titleHeight = wrappedTextHeight(popup, title, titleWidth);
  int firstLineHeight = titleHeight + 10;
  if (firstLineHeight < popup->rowHeight)
    firstLineHeight = popup->rowHeight;
  return firstLineHeight + popup->rowHeight;
}

static int agendaItemsHeight(NativePopup *popup) {
  int height = 0;
  for (size_t row = 0; row < popup->agenda.count; row++)
    height += agendaItemHeight(popup, &popup->agenda.items[row]);
  return height;
}

static int agendaRowAtY(NativePopup *popup, int y) {
  int top = 0;
  for (size_t row = 0; row < popup->agenda.count; row++) {
    int bottom = top + agendaItemHeight(popup, &popup->agenda.items[row]);
    if (y >= top && y < bottom)
      return (int)row;
    top = bottom;
  }
  return -1;
}

static void drawAgenda(NativePopup *popup) {
  setColor(popup->cairo, popup->config.colorPanelBg, "#000000");
  cairo_paint(popup->cairo);
  bool german = panelLanguageIsGerman(&popup->config);
  if (popup->agenda.count == 0) {
    drawEllipsizedText(popup,
                       german ? "Keine anstehenden Termine oder Aufgaben"
                              : "No upcoming events or tasks",
                       12,
                       5,
                       popup->width - 24,
                       popup->config.colorMuted);
  }
  int y = 0;
  for (size_t row = 0; row < popup->agenda.count; row++) {
    const AgendaDisplayItem *display = &popup->agenda.items[row];
    int rowHeight = agendaItemHeight(popup, display);
    if ((int)row == popup->agendaHover) {
      setColor(popup->cairo, popup->config.colorFocusedFreeBg, "#333333");
      cairo_rectangle(popup->cairo, 0, y, popup->width, rowHeight);
      cairo_fill(popup->cairo);
    }
    const char *typeColor = display->overdue
                                ? popup->config.agendaOverdueColor
                                : (display->item.type == AGENDA_ITEM_EVENT
                                       ? popup->config.agendaEventColor
                                       : popup->config.agendaTaskColor);
    const char *symbol = display->item.type == AGENDA_ITEM_EVENT ? "●" : "◆";
    drawText(popup, symbol, 10, y + 5, typeColor);
    int whenX = 30;
    int whenWidth = textWidth(popup, display->when);
    drawText(popup, display->when, whenX, y + 5, typeColor);
    int titleX = whenX + whenWidth + 12;
    int right = popup->width - 10;
    if (display->item.type == AGENDA_ITEM_TASK &&
        popup->config.agendaShowSource && display->item.sourceName[0]) {
      int sourceWidth = textWidth(popup, display->item.sourceName);
      int maximumSourceWidth = popup->width / 4;
      if (sourceWidth > maximumSourceWidth)
        sourceWidth = maximumSourceWidth;
      drawEllipsizedText(popup,
                         display->item.sourceName,
                         right - sourceWidth,
                         y + 5,
                         sourceWidth,
                         popup->config.agendaSourceColor);
      right -= sourceWidth + 12;
    }
    const char *title = display->item.title[0]
                            ? display->item.title
                            : (german ? "(Ohne Titel)" : "(Untitled)");
    if (display->item.type == AGENDA_ITEM_EVENT) {
      int titleWidth = popup->width - titleX - 10;
      if (titleWidth < 1)
        titleWidth = 1;
      drawWrappedText(
          popup, title, titleX, y + 5, titleWidth, popup->config.colorFree);
      char metadata[512];
      formatAgendaMetadata(popup, display, metadata, sizeof(metadata));
      int titleHeight = wrappedTextHeight(popup, title, titleWidth);
      int firstLineHeight = titleHeight + 10;
      if (firstLineHeight < popup->rowHeight)
        firstLineHeight = popup->rowHeight;
      drawEllipsizedText(popup,
                         metadata,
                         whenX,
                         y + firstLineHeight + 5,
                         popup->width - whenX - 10,
                         popup->config.agendaSourceColor);
    } else {
      drawEllipsizedText(
          popup, title, titleX, y + 5, right - titleX, popup->config.colorFree);
    }
    y += rowHeight;
  }
  if (popup->agenda.hiddenEvents || popup->agenda.hiddenTasks) {
    setColor(popup->cairo, popup->config.colorBg, "#222222");
    cairo_rectangle(popup->cairo, 0, y, popup->width, popup->rowHeight);
    cairo_fill(popup->cairo);
    char events[96] = "", tasks[96] = "";
    if (popup->agenda.hiddenEvents)
      snprintf(events,
               sizeof(events),
               german ? "+ %zu weitere Termine" : "+ %zu more events",
               popup->agenda.hiddenEvents);
    if (popup->agenda.hiddenTasks)
      snprintf(tasks,
               sizeof(tasks),
               german ? "+ %zu weitere Aufgaben" : "+ %zu more tasks",
               popup->agenda.hiddenTasks);
    drawText(popup, events, 10, y + 5, popup->config.agendaEventColor);
    if (tasks[0])
      drawRightAlignedText(popup,
                           tasks,
                           popup->width - 10,
                           y + 5,
                           popup->config.agendaTaskColor);
  }
}

static void drawPopup(NativePopup *popup) {
  if (!popup->open)
    return;
  if (popup->mode == POPUP_MODE_FORECAST)
    drawForecast(popup);
  else if (popup->mode == POPUP_MODE_AGENDA)
    drawAgenda(popup);
  else if (popup->mode == POPUP_MODE_INFO) {
    setColor(popup->cairo, popup->config.colorPanelBg, "#000000");
    cairo_paint(popup->cairo);
    for (size_t row = 0; row < popup->itemCount; row++)
      drawEllipsizedText(popup,
                         popup->items[row].label,
                         12,
                         (int)row * popup->rowHeight + 5,
                         popup->width - 24,
                         popup->config.colorFree);
  } else
    drawMenu(popup);
  cairo_surface_flush(popup->surface);
  xcb_flush(popup->connection);
}

#ifdef HAVE_XKBCOMMON_X11
static bool initializeKeyboard(NativePopup *popup) {
  popup->xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkb_x11_setup_xkb_extension(popup->connection,
                                   XKB_X11_MIN_MAJOR_XKB_VERSION,
                                   XKB_X11_MIN_MINOR_XKB_VERSION,
                                   XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL))
    return false;
  int device = xkb_x11_get_core_keyboard_device_id(popup->connection);
  if (!popup->xkbContext || device < 0)
    return false;
  popup->keymap = xkb_x11_keymap_new_from_device(popup->xkbContext,
                                                 popup->connection,
                                                 device,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
  popup->xkbState =
      xkb_x11_state_new_from_device(popup->keymap, popup->connection, device);
  const char *locale = setlocale(LC_CTYPE, NULL);
  popup->composeTable = xkb_compose_table_new_from_locale(
      popup->xkbContext, locale ? locale : "C", XKB_COMPOSE_COMPILE_NO_FLAGS);
  if (popup->composeTable)
    popup->composeState =
        xkb_compose_state_new(popup->composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
  return popup->keymap && popup->xkbState;
}
#endif

NativePopup *nativePopupCreate(xcb_connection_t *connection,
                               xcb_screen_t *screen,
                               const PanelConfig *config) {
  xcb_visualtype_t *visual = findVisual(screen);
  if (!visual)
    return NULL;
  NativePopup *popup = calloc(1, sizeof(*popup));
  if (!popup)
    return NULL;
  popup->connection = connection;
  popup->screen = screen;
  popup->config = *config;
  popup->width = POPUP_WIDTH;
  popup->anchorWidth = (int)screen->width_in_pixels;
  popup->panelHeight = config->height;
  popup->rowHeight = config->height > 28 ? config->height : 28;
  popup->window = xcb_generate_id(connection);
  uint32_t values[] = {
      screen->black_pixel,
      1,
      XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS |
          XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS |
          XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_FOCUS_CHANGE,
  };
  xcb_create_window(connection,
                    screen->root_depth,
                    popup->window,
                    screen->root,
                    0,
                    (int16_t)config->height,
                    (uint16_t)popup->width,
                    (uint16_t)popup->rowHeight,
                    1,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
                        XCB_CW_EVENT_MASK,
                    values);
  popup->surface = cairo_xcb_surface_create(
      connection, popup->window, visual, popup->width, popup->rowHeight);
  popup->cairo = cairo_create(popup->surface);
  popup->layout = pango_cairo_create_layout(popup->cairo);
  char description[256];
  fontDescription(config->font, description, sizeof(description));
  popup->font = pango_font_description_from_string(description);
  if (config->iconFont[0]) {
    fontDescription(config->iconFont, description, sizeof(description));
    popup->iconFont = pango_font_description_from_string(description);
    popup->hasIconFont = fontAvailable(popup->layout, popup->iconFont);
    if (!popup->hasIconFont) {
      pango_font_description_free(popup->iconFont);
      popup->iconFont = pango_font_description_copy(popup->font);
    }
  } else {
    popup->iconFont = pango_font_description_copy(popup->font);
  }
  pango_layout_set_font_description(popup->layout, popup->font);
#ifdef HAVE_XKBCOMMON_X11
  if (!initializeKeyboard(popup)) {
    nativePopupDestroy(popup);
    return NULL;
  }
#endif
  return popup;
}

void nativePopupDestroy(NativePopup *popup) {
  if (!popup)
    return;
  nativePopupClose(popup);
#ifdef HAVE_XKBCOMMON_X11
  if (popup->composeState)
    xkb_compose_state_unref(popup->composeState);
  if (popup->composeTable)
    xkb_compose_table_unref(popup->composeTable);
  if (popup->xkbState)
    xkb_state_unref(popup->xkbState);
  if (popup->keymap)
    xkb_keymap_unref(popup->keymap);
  if (popup->xkbContext)
    xkb_context_unref(popup->xkbContext);
#endif
  if (popup->layout)
    g_object_unref(popup->layout);
  if (popup->font)
    pango_font_description_free(popup->font);
  if (popup->iconFont)
    pango_font_description_free(popup->iconFont);
  if (popup->cairo)
    cairo_destroy(popup->cairo);
  if (popup->surface)
    cairo_surface_destroy(popup->surface);
  if (popup->window)
    xcb_destroy_window(popup->connection, popup->window);
  free(popup);
}

bool nativePopupAvailable(const NativePopup *popup) {
#ifdef HAVE_XKBCOMMON_X11
  return popup && popup->xkbState;
#else
  (void)popup;
  return false;
#endif
}

bool nativePopupIsOpen(const NativePopup *popup) {
  return popup && popup->open;
}

static bool nativePopupHasFocus(const NativePopup *popup) {
  xcb_get_input_focus_reply_t *focusReply = xcb_get_input_focus_reply(
      popup->connection, xcb_get_input_focus(popup->connection), NULL);
  bool focused = focusReply && focusReply->focus == popup->window;
  free(focusReply);
  return focused;
}

void nativePopupSetBounds(
    NativePopup *popup, int x, int y, int width, int panelHeight) {
  if (!popup || width <= 0)
    return;
  popup->anchorX = x;
  popup->anchorY = y;
  popup->anchorWidth = width;
  popup->panelHeight = panelHeight;
  if (popup->open)
    nativePopupClose(popup);
}

static void configurePopupWindow(NativePopup *popup) {
  uint32_t geometry[] = {(uint32_t)popup->x,
                         (uint32_t)popup->y,
                         (uint32_t)popup->width,
                         (uint32_t)popup->height};
  xcb_configure_window(popup->connection,
                       popup->window,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                       geometry);
  cairo_xcb_surface_set_size(popup->surface, popup->width, popup->height);
}

static void showPopup(NativePopup *popup, bool takeFocus) {
  configurePopupWindow(popup);
  xcb_map_window(popup->connection, popup->window);
  const uint32_t STACK_MODE = XCB_STACK_MODE_ABOVE;
  xcb_configure_window(popup->connection,
                       popup->window,
                       XCB_CONFIG_WINDOW_STACK_MODE,
                       &STACK_MODE);
  if (takeFocus) {
    if (!popup->open) {
      xcb_get_input_focus_reply_t *focusReply = xcb_get_input_focus_reply(
          popup->connection, xcb_get_input_focus(popup->connection), NULL);
      popup->focusSaved = focusReply != NULL;
      if (focusReply) {
        popup->previousFocus = focusReply->focus;
        popup->previousRevertTo = focusReply->revert_to;
      }
      free(focusReply);
    }
    xcb_set_input_focus(popup->connection,
                        XCB_INPUT_FOCUS_POINTER_ROOT,
                        popup->window,
                        XCB_CURRENT_TIME);
  }
  popup->open = true;
  drawPopup(popup);
}

static int prepareMenuPopup(NativePopup *popup,
                            const PopupItem *items,
                            size_t count,
                            bool searchable) {
  if (!nativePopupAvailable(popup) || !items || count == 0)
    return -1;
  if (count > POPUP_ITEM_MAX)
    count = POPUP_ITEM_MAX;
  popup->mode = POPUP_MODE_MENU;
  memcpy(popup->items, items, count * sizeof(*items));
  popup->itemCount = count;
  popup->searchable = searchable;
  popup->query[0] = '\0';
  updateMatches(popup);
  size_t rows = count < POPUP_VISIBLE_ROWS ? count : POPUP_VISIBLE_ROWS;
  if (rows == 0)
    rows = 1;
  popup->height = (int)(rows + (searchable ? 1U : 0U)) * popup->rowHeight;
  popup->width =
      popup->anchorWidth < POPUP_WIDTH ? popup->anchorWidth : POPUP_WIDTH;
  popup->y = popup->anchorY + popup->panelHeight;
  return 0;
}

static int
popupXAtAction(const NativePopup *popup, int actionX, int actionWidth) {
  int x = actionX + actionWidth / 2 - popup->width / 2;
  if (x < popup->anchorX)
    x = popup->anchorX;
  int maximumX = popup->anchorX + popup->anchorWidth - popup->width;
  if (x > maximumX)
    x = maximumX;
  return x;
}

int nativePopupOpen(NativePopup *popup,
                    const PopupItem *items,
                    size_t count,
                    bool searchable,
                    bool anchorRight) {
  if (prepareMenuPopup(popup, items, count, searchable))
    return -1;
  popup->x = anchorRight ? popup->anchorX + popup->anchorWidth - popup->width
                         : popup->anchorX;
  showPopup(popup, true);
  return 0;
}

int nativePopupOpenAt(NativePopup *popup,
                      const PopupItem *items,
                      size_t count,
                      bool searchable,
                      int actionX,
                      int actionWidth) {
  if (prepareMenuPopup(popup, items, count, searchable))
    return -1;
  popup->x = popupXAtAction(popup, actionX, actionWidth);
  showPopup(popup, true);
  return 0;
}

int nativePopupOpenInfoAt(NativePopup *popup,
                          const PopupItem *items,
                          size_t count,
                          int actionX,
                          int actionWidth) {
  if (prepareMenuPopup(popup, items, count, false))
    return -1;
  popup->x = popupXAtAction(popup, actionX, actionWidth);
  popup->mode = POPUP_MODE_INFO;
  showPopup(popup, true);
  return 0;
}

static void configureForecastGeometry(NativePopup *popup) {
  popup->width =
      popup->anchorWidth < FORECAST_WIDTH ? popup->anchorWidth : FORECAST_WIDTH;
  popup->height = popup->forecast.dayCount > 0
                      ? FORECAST_TITLE_HEIGHT +
                            WEATHER_FORECAST_DAY_COUNT * FORECAST_DAY_HEIGHT
                      : FORECAST_TITLE_HEIGHT + 2 * popup->rowHeight;
  int right = popup->forecastAnchorX + popup->forecastAnchorWidth;
  popup->x = right - popup->width;
  if (popup->x < popup->anchorX)
    popup->x = popup->anchorX;
  int maximumX = popup->anchorX + popup->anchorWidth - popup->width;
  if (popup->x > maximumX)
    popup->x = maximumX;
  popup->y = popup->anchorY + popup->panelHeight;
}

int nativePopupOpenForecast(NativePopup *popup,
                            const WeatherForecast *forecast,
                            const char *location,
                            int actionX,
                            int actionWidth) {
  if (!nativePopupAvailable(popup) || !forecast)
    return -1;
  popup->mode = POPUP_MODE_FORECAST;
  popup->searchable = false;
  popup->itemCount = 0;
  popup->matchCount = 0;
  popup->forecast = *forecast;
  snprintf(popup->forecastLocation,
           sizeof(popup->forecastLocation),
           "%s",
           location ? location : "");
  popup->forecastAnchorX = actionX;
  popup->forecastAnchorWidth = actionWidth > 0 ? actionWidth : 1;
  configureForecastGeometry(popup);
  showPopup(popup, true);
  return 0;
}

void nativePopupUpdateForecast(NativePopup *popup,
                               const WeatherForecast *forecast,
                               const char *location) {
  if (!popup || !forecast || !nativePopupIsForecastOpen(popup))
    return;
  popup->forecast = *forecast;
  snprintf(popup->forecastLocation,
           sizeof(popup->forecastLocation),
           "%s",
           location ? location : "");
  configureForecastGeometry(popup);
  configurePopupWindow(popup);
  drawPopup(popup);
}

bool nativePopupIsForecastOpen(const NativePopup *popup) {
  return popup && popup->open && popup->mode == POPUP_MODE_FORECAST;
}

static bool configureAgendaGeometry(NativePopup *popup) {
  int marginWidth = popup->anchorWidth - 16;
  popup->width = (int)popup->config.agendaPopupWidth;
  if (popup->width > marginWidth)
    popup->width = marginWidth;
  popup->height = popup->agenda.count ? 0 : popup->rowHeight;
  for (size_t row = 0; row < popup->agenda.count; row++)
    popup->height += agendaItemHeight(popup, &popup->agenda.items[row]);
  if (popup->agenda.hiddenEvents || popup->agenda.hiddenTasks)
    popup->height += popup->rowHeight;
  int right = popup->agendaAnchorX + popup->agendaAnchorWidth;
  if (popup->agendaAnchorX + popup->agendaAnchorWidth / 2 >=
      popup->anchorX + popup->anchorWidth / 2)
    popup->x = right - popup->width;
  else
    popup->x = popup->agendaAnchorX;
  int minimumX = popup->anchorX + 8;
  int maximumX = popup->anchorX + popup->anchorWidth - popup->width - 8;
  if (popup->x < minimumX)
    popup->x = minimumX;
  if (popup->x > maximumX)
    popup->x = maximumX;
  int below = popup->anchorY + popup->panelHeight;
  int screenBottom = (int)popup->screen->height_in_pixels;
  if (below + popup->height <= screenBottom)
    popup->y = below;
  else if (popup->anchorY - popup->height >= 0)
    popup->y = popup->anchorY - popup->height;
  else
    return false;
  return true;
}

int nativePopupOpenAgenda(NativePopup *popup,
                          const AgendaView *agenda,
                          int actionX,
                          int actionWidth) {
  if (!nativePopupAvailable(popup) || !agenda || !agenda->available)
    return -1;
  popup->mode = POPUP_MODE_AGENDA;
  popup->searchable = false;
  popup->agenda = *agenda;
  popup->agendaAnchorX = actionX;
  popup->agendaAnchorWidth = actionWidth > 0 ? actionWidth : 1;
  popup->agendaHover = -1;
  if (!configureAgendaGeometry(popup))
    return -1;
  showPopup(popup, false);
  xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(
      popup->connection,
      0,
      popup->screen->root,
      XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION,
      XCB_GRAB_MODE_ASYNC,
      XCB_GRAB_MODE_ASYNC,
      XCB_WINDOW_NONE,
      XCB_CURSOR_NONE,
      XCB_CURRENT_TIME);
  xcb_grab_pointer_reply_t *reply =
      xcb_grab_pointer_reply(popup->connection, cookie, NULL);
  popup->pointerGrabbed = reply && reply->status == XCB_GRAB_STATUS_SUCCESS;
  free(reply);
  if (!popup->pointerGrabbed) {
    nativePopupClose(popup);
    return -1;
  }
  return 0;
}

void nativePopupUpdateAgenda(NativePopup *popup, const AgendaView *agenda) {
  if (!popup || !agenda || !nativePopupIsAgendaOpen(popup))
    return;
  if (!agenda->available) {
    nativePopupClose(popup);
    return;
  }
  popup->agenda = *agenda;
  popup->agendaHover = -1;
  if (!configureAgendaGeometry(popup)) {
    nativePopupClose(popup);
    return;
  }
  configurePopupWindow(popup);
  drawPopup(popup);
}

bool nativePopupIsAgendaOpen(const NativePopup *popup) {
  return popup && popup->open && popup->mode == POPUP_MODE_AGENDA;
}

void nativePopupGeometry(
    const NativePopup *popup, int *x, int *y, int *width, int *height) {
  if (!popup)
    return;
  if (x)
    *x = popup->x;
  if (y)
    *y = popup->y;
  if (width)
    *width = popup->width;
  if (height)
    *height = popup->height;
}

void nativePopupClose(NativePopup *popup) {
  if (!popup || !popup->open)
    return;
  xcb_get_input_focus_reply_t *focusReply = xcb_get_input_focus_reply(
      popup->connection, xcb_get_input_focus(popup->connection), NULL);
  bool restoreFocus = focusReply && focusReply->focus == popup->window &&
                      popup->focusSaved &&
                      popup->previousFocus != XCB_INPUT_FOCUS_NONE;
  free(focusReply);
  xcb_unmap_window(popup->connection, popup->window);
  if (popup->pointerGrabbed)
    xcb_ungrab_pointer(popup->connection, XCB_CURRENT_TIME);
  if (restoreFocus)
    xcb_set_input_focus(popup->connection,
                        popup->previousRevertTo,
                        popup->previousFocus,
                        XCB_CURRENT_TIME);
  xcb_flush(popup->connection);
  popup->focusSaved = false;
  popup->pointerGrabbed = false;
  popup->open = false;
}

static void ensureSelectedVisible(NativePopup *popup) {
  if (popup->selected < popup->top)
    popup->top = popup->selected;
  if (popup->selected >= popup->top + POPUP_VISIBLE_ROWS)
    popup->top = popup->selected - POPUP_VISIBLE_ROWS + 1;
}

static void
chooseSelected(NativePopup *popup, char *action, size_t actionSize) {
  if (popup->selected < popup->matchCount)
    snprintf(action,
             actionSize,
             "%s",
             popup->items[popup->matches[popup->selected]].action);
  nativePopupClose(popup);
}

#ifdef HAVE_XKBCOMMON_X11
static void appendInput(NativePopup *popup, const char *text) {
  if (!popup->searchable || !text || !*text)
    return;
  size_t used = strlen(popup->query), length = strlen(text);
  if (used + length >= sizeof(popup->query))
    return;
  memcpy(popup->query + used, text, length + 1);
  updateMatches(popup);
}

static void handleKey(NativePopup *popup,
                      const xcb_key_press_event_t *key,
                      char *action,
                      size_t actionSize) {
  xkb_state_update_key(popup->xkbState, key->detail, XKB_KEY_DOWN);
  xkb_keysym_t symbol = xkb_state_key_get_one_sym(popup->xkbState, key->detail);
  if (symbol == XKB_KEY_Escape) {
    nativePopupClose(popup);
  } else if (popup->mode == POPUP_MODE_FORECAST) {
    return;
  } else if (symbol == XKB_KEY_Return || symbol == XKB_KEY_KP_Enter) {
    chooseSelected(popup, action, actionSize);
  } else if (symbol == XKB_KEY_Up && popup->selected > 0) {
    popup->selected--;
  } else if (symbol == XKB_KEY_Down &&
             popup->selected + 1 < popup->matchCount) {
    popup->selected++;
  } else if (symbol == XKB_KEY_Page_Up) {
    popup->selected = popup->selected > POPUP_VISIBLE_ROWS
                          ? popup->selected - POPUP_VISIBLE_ROWS
                          : 0;
  } else if (symbol == XKB_KEY_Page_Down && popup->matchCount) {
    popup->selected += POPUP_VISIBLE_ROWS;
    if (popup->selected >= popup->matchCount)
      popup->selected = popup->matchCount - 1;
  } else if (symbol == XKB_KEY_Home) {
    popup->selected = 0;
  } else if (symbol == XKB_KEY_End && popup->matchCount) {
    popup->selected = popup->matchCount - 1;
  } else if (symbol == XKB_KEY_BackSpace && popup->searchable &&
             popup->query[0]) {
    char *previous = g_utf8_find_prev_char(popup->query,
                                           popup->query + strlen(popup->query));
    if (previous)
      *previous = '\0';
    updateMatches(popup);
  } else if (popup->searchable) {
    char text[64] = "";
    if (popup->composeState) {
      xkb_compose_state_feed(popup->composeState, symbol);
      enum xkb_compose_status status =
          xkb_compose_state_get_status(popup->composeState);
      if (status == XKB_COMPOSE_COMPOSED) {
        xkb_compose_state_get_utf8(popup->composeState, text, sizeof(text));
        xkb_compose_state_reset(popup->composeState);
      } else if (status == XKB_COMPOSE_CANCELLED) {
        xkb_compose_state_reset(popup->composeState);
      } else if (status == XKB_COMPOSE_NOTHING) {
        xkb_state_key_get_utf8(
            popup->xkbState, key->detail, text, sizeof(text));
      }
    } else {
      xkb_state_key_get_utf8(popup->xkbState, key->detail, text, sizeof(text));
    }
    appendInput(popup, text);
  }
  ensureSelectedVisible(popup);
  if (popup->open)
    drawPopup(popup);
}
#endif

bool nativePopupHandleEvent(NativePopup *popup,
                            const xcb_generic_event_t *event,
                            char *action,
                            size_t actionSize,
                            bool *redraw) {
  if (!popup || !popup->open || !event)
    return false;
  uint8_t type = event->response_type & 0x7fU;
  if (type == XCB_EXPOSE &&
      ((const xcb_expose_event_t *)event)->window == popup->window) {
    drawPopup(popup);
    *redraw = false;
    return true;
  }
  if (type == XCB_BUTTON_PRESS) {
    const xcb_button_press_event_t *button =
        (const xcb_button_press_event_t *)event;
    if (button->root_x < popup->x ||
        button->root_x >= popup->x + popup->width ||
        button->root_y < popup->y ||
        button->root_y >= popup->y + popup->height) {
      nativePopupClose(popup);
      return true;
    }
    if (popup->mode == POPUP_MODE_FORECAST)
      return true;
    if (popup->mode == POPUP_MODE_AGENDA) {
      if (button->detail != 1)
        return true;
      int contentY = button->root_y - popup->y;
      int row = agendaRowAtY(popup, contentY);
      if (row >= 0) {
        snprintf(action,
                 actionSize,
                 "role|%s",
                 popup->agenda.items[(size_t)row].item.type == AGENDA_ITEM_EVENT
                     ? "calendar"
                     : "tasks");
        return true;
      }
      if (contentY >= agendaItemsHeight(popup) &&
          (popup->agenda.hiddenEvents || popup->agenda.hiddenTasks)) {
        bool calendar = popup->agenda.hiddenEvents &&
                        (!popup->agenda.hiddenTasks ||
                         button->root_x < popup->x + popup->width / 2);
        snprintf(
            action, actionSize, "role|%s", calendar ? "calendar" : "tasks");
      }
      return true;
    }
    if (popup->mode == POPUP_MODE_INFO)
      return false;
    if (button->detail == 4 && popup->selected > 0) {
      popup->selected--;
      ensureSelectedVisible(popup);
      drawPopup(popup);
      return true;
    }
    if (button->detail == 5 && popup->selected + 1 < popup->matchCount) {
      popup->selected++;
      ensureSelectedVisible(popup);
      drawPopup(popup);
      return true;
    }
    if (button->detail != 1)
      return true;
    int contentY =
        button->root_y - popup->y - (popup->searchable ? popup->rowHeight : 0);
    if (contentY >= 0) {
      size_t row = (size_t)(contentY / popup->rowHeight);
      size_t position = popup->top + row;
      if (position < popup->matchCount) {
        popup->selected = position;
        chooseSelected(popup, action, actionSize);
      }
    }
    return true;
  }
  if (type == XCB_MOTION_NOTIFY && popup->mode == POPUP_MODE_AGENDA) {
    const xcb_motion_notify_event_t *motion =
        (const xcb_motion_notify_event_t *)event;
    int hover = -1;
    if (motion->root_x >= popup->x &&
        motion->root_x < popup->x + popup->width &&
        motion->root_y >= popup->y &&
        motion->root_y < popup->y + popup->height) {
      hover = agendaRowAtY(popup, motion->root_y - popup->y);
    }
    if (hover != popup->agendaHover) {
      popup->agendaHover = hover;
      drawPopup(popup);
    }
    return true;
  }
#ifdef HAVE_XKBCOMMON_X11
  if (type == XCB_KEY_PRESS) {
    handleKey(popup, (const xcb_key_press_event_t *)event, action, actionSize);
    return true;
  }
  if (type == XCB_KEY_RELEASE) {
    const xcb_key_release_event_t *key = (const xcb_key_release_event_t *)event;
    xkb_state_update_key(popup->xkbState, key->detail, XKB_KEY_UP);
    return true;
  }
#endif
  if (type == XCB_FOCUS_OUT && popup->mode != POPUP_MODE_AGENDA) {
    if (!nativePopupHasFocus(popup))
      nativePopupClose(popup);
    return true;
  }
  return false;
}

#endif
