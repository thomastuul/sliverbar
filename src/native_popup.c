#include "native_popup.h"

#include "app_launcher.h"

#ifdef HAVE_NATIVE_PANEL

#include <cairo/cairo-xcb.h>
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
#define FORECAST_WIDTH 680
#define FORECAST_TITLE_HEIGHT 34
#define FORECAST_DAY_HEIGHT 120

typedef enum {
  POPUP_MODE_MENU,
  POPUP_MODE_FORECAST,
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
  xcb_window_t previousFocus;
  uint8_t previousRevertTo;
  bool focusSaved;
  bool open;
  bool searchable;
  bool hasIconFont;
  PopupMode mode;
  char query[256];
  WeatherForecast forecast;
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
  if (!value || value[0] != '#' || strlen(value) != 7 ||
      sscanf(value + 1, "%06x", &rgb) != 1)
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
formatTemperature(bool valid, int temperature, char *output, size_t size) {
  if (valid)
    snprintf(output, size, "%d°", temperature);
  else
    snprintf(output, size, "%s", "–");
}

static void formatDayTitle(const NativePopup *popup,
                           const WeatherForecastDay *day,
                           char *output,
                           size_t size) {
  char weekday[32], minimum[16], maximum[16];
  weatherForecastDayName(day->date,
                         panelLanguageIsGerman(&popup->config),
                         weekday,
                         sizeof(weekday));
  formatTemperature(day->minimumValid, day->minimumC, minimum, sizeof(minimum));
  formatTemperature(day->maximumValid, day->maximumC, maximum, sizeof(maximum));
  snprintf(output, size, "%s · %s–%s", weekday, minimum, maximum);
}

static void drawTimeField(NativePopup *popup,
                          const char *label,
                          int x,
                          int y,
                          int width,
                          int height) {
  setColor(popup->cairo, popup->config.colorBg, "#222222");
  cairo_rectangle(popup->cairo, x + 3, y + 2, width - 6, height - 4);
  cairo_fill_preserve(popup->cairo);
  setColor(popup->cairo, popup->config.colorFree, "#bfbfbf");
  cairo_set_line_width(popup->cairo, 1.0);
  cairo_stroke(popup->cairo);
  drawCenteredText(popup,
                   label,
                   x + 3,
                   y + 2,
                   width - 6,
                   height - 4,
                   popup->config.colorClock);
}

static void drawForecast(NativePopup *popup) {
  setColor(popup->cairo, popup->config.colorPanelBg, "#000000");
  cairo_paint(popup->cairo);
  bool german = panelLanguageIsGerman(&popup->config);
  const char *title = popup->forecastLocation[0]
                          ? popup->forecastLocation
                          : (german ? "Wettervorhersage" : "Weather forecast");
  char updated[64];
  weatherForecastUpdatedLabel(popup->forecast.updatedAt,
                              popup->forecast.updatedAtValid,
                              german,
                              time(NULL),
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

  int labelWidth = popup->width >= 560 ? 104 : popup->width / 5;
  if (labelWidth < 72)
    labelWidth = 72;
  int contentWidth = popup->width - labelWidth - 8;
  int slotWidth = contentWidth / WEATHER_FORECAST_SLOT_COUNT;
  int remainder = contentWidth % WEATHER_FORECAST_SLOT_COUNT;
  const int HEADER_HEIGHT = 26;
  const int TIME_HEIGHT = 24;
  const int VALUE_HEIGHT = 22;
  for (size_t dayIndex = 0; dayIndex < WEATHER_FORECAST_DAY_COUNT; dayIndex++) {
    const WeatherForecastDay *day = &popup->forecast.days[dayIndex];
    int dayY = FORECAST_TITLE_HEIGHT + (int)dayIndex * FORECAST_DAY_HEIGHT;
    setColor(popup->cairo, popup->config.colorBg, "#222222");
    cairo_rectangle(popup->cairo, 0, dayY, popup->width, FORECAST_DAY_HEIGHT);
    cairo_fill(popup->cairo);
    char dayTitle[96];
    if (day->available)
      formatDayTitle(popup, day, dayTitle, sizeof(dayTitle));
    else
      snprintf(dayTitle, sizeof(dayTitle), "%s", "–");
    drawText(popup, dayTitle, 10, dayY + 4, popup->config.colorFocusedFree);
    int timeY = dayY + HEADER_HEIGHT;
    int weatherY = timeY + TIME_HEIGHT;
    int temperatureY = weatherY + VALUE_HEIGHT;
    int rainY = temperatureY + VALUE_HEIGHT;
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
    int slotX = labelWidth;
    for (size_t slotIndex = 0; slotIndex < WEATHER_FORECAST_SLOT_COUNT;
         slotIndex++) {
      int width = slotWidth + ((int)slotIndex < remainder ? 1 : 0);
      const WeatherForecastSlot *slot = &day->slots[slotIndex];
      char text[16];
      snprintf(text, sizeof(text), "%02d", slot->hour);
      drawTimeField(popup, text, slotX, timeY, width, TIME_HEIGHT);
      const char *glyph =
          slot->codeValid
              ? weatherConditionGlyph(slot->condition, popup->hasIconFont)
              : "–";
      drawCenteredText(popup,
                       glyph,
                       slotX,
                       weatherY,
                       width,
                       VALUE_HEIGHT,
                       popup->config.colorWeather);
      formatTemperature(
          slot->temperatureValid, slot->temperatureC, text, sizeof(text));
      drawCenteredText(popup,
                       text,
                       slotX,
                       temperatureY,
                       width,
                       VALUE_HEIGHT,
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
                       VALUE_HEIGHT,
                       popup->config.colorNetwork);
      slotX += width;
    }
  }
}

static void drawPopup(NativePopup *popup) {
  if (!popup->open)
    return;
  if (popup->mode == POPUP_MODE_FORECAST)
    drawForecast(popup);
  else
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
          XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
          XCB_EVENT_MASK_FOCUS_CHANGE,
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

static void showPopup(NativePopup *popup) {
  configurePopupWindow(popup);
  xcb_map_window(popup->connection, popup->window);
  const uint32_t STACK_MODE = XCB_STACK_MODE_ABOVE;
  xcb_configure_window(popup->connection,
                       popup->window,
                       XCB_CONFIG_WINDOW_STACK_MODE,
                       &STACK_MODE);
  xcb_get_input_focus_reply_t *focusReply = xcb_get_input_focus_reply(
      popup->connection, xcb_get_input_focus(popup->connection), NULL);
  popup->focusSaved = focusReply != NULL;
  if (focusReply) {
    popup->previousFocus = focusReply->focus;
    popup->previousRevertTo = focusReply->revert_to;
  }
  free(focusReply);
  xcb_set_input_focus(popup->connection,
                      XCB_INPUT_FOCUS_POINTER_ROOT,
                      popup->window,
                      XCB_CURRENT_TIME);
  popup->open = true;
  drawPopup(popup);
}

int nativePopupOpen(NativePopup *popup,
                    const PopupItem *items,
                    size_t count,
                    bool searchable,
                    bool anchorRight) {
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
  popup->x = anchorRight ? popup->anchorX + popup->anchorWidth - popup->width
                         : popup->anchorX;
  popup->y = popup->anchorY + popup->panelHeight;
  showPopup(popup);
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
  showPopup(popup);
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
  if (restoreFocus)
    xcb_set_input_focus(popup->connection,
                        popup->previousRevertTo,
                        popup->previousFocus,
                        XCB_CURRENT_TIME);
  xcb_flush(popup->connection);
  popup->focusSaved = false;
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
  if (type == XCB_FOCUS_OUT) {
    if (!nativePopupHasFocus(popup))
      nativePopupClose(popup);
    return true;
  }
  return false;
}

#endif
