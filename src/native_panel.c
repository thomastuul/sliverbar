#include "native_panel.h"
#include "native_tray.h"

#ifdef HAVE_NATIVE_PANEL

#include <cairo/cairo-xcb.h>
#include <pango/pangocairo.h>
#ifdef HAVE_XCB_RANDR
#include <xcb/randr.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEGMENTS 256
#define MAX_REGIONS 256
#define MAX_ACTION_DEPTH 32

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } Alignment;

typedef struct {
  char foreground[16];
  char background[16];
  char underlineColor[16];
  bool underline;
} DrawStyle;

typedef struct {
  Alignment align;
  int offset;
  int width;
  bool tray;
  DrawStyle style;
  char text[512];
  char actions[5][256];
} Segment;

typedef struct {
  int x0;
  int x1;
  uint8_t button;
  char command[256];
} ActionRegion;

typedef struct {
  uint8_t button;
  char command[256];
} ActionEntry;

struct NativePanel {
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t window;
  cairo_surface_t *surface;
  cairo_surface_t *backSurface;
  cairo_t *cairo;
  cairo_t *presentCairo;
  PangoLayout *layout;
  PangoFontDescription *font;
  PangoFontDescription *iconFont;
  NativeTray *tray;
  PanelConfig config;
  int x;
  int y;
  int width;
#ifdef HAVE_XCB_RANDR
  int randrEventBase;
#endif
  bool mapped;
  ActionRegion regions[MAX_REGIONS];
  size_t regionCount;
  char lastMarkup[32768];
  bool repaintRequested;
};

static void useRootBounds(NativePanel *panel) {
  panel->x = 0;
  panel->y = 0;
  panel->width = panel->screen->width_in_pixels;
}

#ifdef HAVE_XCB_RANDR
static bool monitorNameMatches(NativePanel *panel,
                               xcb_atom_t name,
                               const char *configured) {
  xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(
      panel->connection, xcb_get_atom_name(panel->connection, name), NULL);
  if (!reply)
    return false;
  int length = xcb_get_atom_name_name_length(reply);
  bool matches =
      length >= 0 && length == (int)strlen(configured) &&
      !memcmp(xcb_get_atom_name_name(reply), configured, (size_t)length);
  free(reply);
  return matches;
}

static void resolveMonitorBounds(NativePanel *panel) {
  useRootBounds(panel);
  if (!strcmp(panel->config.monitor, "all"))
    return;
  xcb_randr_get_monitors_reply_t *reply = xcb_randr_get_monitors_reply(
      panel->connection,
      xcb_randr_get_monitors(panel->connection, panel->screen->root, 1),
      NULL);
  if (!reply)
    return;
  char *end = NULL;
  long requestedIndex = strtol(panel->config.monitor, &end, 10);
  bool useIndex =
      end != panel->config.monitor && *end == '\0' && requestedIndex >= 0;
  int index = 0;
  xcb_randr_monitor_info_iterator_t monitors =
      xcb_randr_get_monitors_monitors_iterator(reply);
  for (; monitors.rem; xcb_randr_monitor_info_next(&monitors), index++) {
    xcb_randr_monitor_info_t *monitor = monitors.data;
    bool selected =
        (!strcmp(panel->config.monitor, "primary") && monitor->primary) ||
        (useIndex && requestedIndex == index) ||
        (!useIndex && strcmp(panel->config.monitor, "primary") != 0 &&
         monitorNameMatches(panel, monitor->name, panel->config.monitor));
    if (!selected)
      continue;
    panel->x = monitor->x;
    panel->y = monitor->y;
    panel->width = monitor->width;
    break;
  }
  free(reply);
}

static void subscribeRandr(NativePanel *panel) {
  const xcb_query_extension_reply_t *extension =
      xcb_get_extension_data(panel->connection, &xcb_randr_id);
  if (!extension || !extension->present)
    return;
  panel->randrEventBase = extension->first_event;
  xcb_randr_select_input(panel->connection,
                         panel->screen->root,
                         XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                             XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                             XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
}
#else
static void resolveMonitorBounds(NativePanel *panel) {
  useRootBounds(panel);
}
#endif

static xcb_atom_t internAtom(xcb_connection_t *connection, const char *name) {
  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
  xcb_intern_atom_reply_t *reply =
      xcb_intern_atom_reply(connection, cookie, NULL);
  xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
  free(reply);
  return result;
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

static int createBackBuffer(NativePanel *panel) {
  cairo_surface_t *surface = cairo_image_surface_create(
      CAIRO_FORMAT_RGB24, panel->width, panel->config.height);
  cairo_t *cairo = cairo_create(surface);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS ||
      cairo_status(cairo) != CAIRO_STATUS_SUCCESS) {
    cairo_destroy(cairo);
    cairo_surface_destroy(surface);
    return -1;
  }
  if (panel->cairo)
    cairo_destroy(panel->cairo);
  if (panel->backSurface)
    cairo_surface_destroy(panel->backSurface);
  panel->backSurface = surface;
  panel->cairo = cairo;
  panel->repaintRequested = true;
  return 0;
}

static bool
parseColor(const char *value, double *red, double *green, double *blue) {
  unsigned rgb;
  if (!value || value[0] != '#' || strlen(value) != 7 ||
      sscanf(value + 1, "%06x", &rgb) != 1)
    return false;
  *red = (double)((rgb >> 16) & 0xffU) / 255.0;
  *green = (double)((rgb >> 8) & 0xffU) / 255.0;
  *blue = (double)(rgb & 0xffU) / 255.0;
  return true;
}

static void setColor(cairo_t *cairo, const char *value, const char *fallback) {
  double red = 0.0, green = 0.0, blue = 0.0;
  if (!parseColor(value, &red, &green, &blue))
    parseColor(fallback, &red, &green, &blue);
  cairo_set_source_rgb(cairo, red, green, blue);
}

static void fontName(const char *configured, char *output, size_t size) {
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

static void setCardinal(xcb_connection_t *connection,
                        xcb_window_t window,
                        xcb_atom_t property,
                        const uint32_t *values,
                        uint32_t count) {
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      property,
                      XCB_ATOM_CARDINAL,
                      32,
                      count,
                      values);
}

static void configureEwmh(NativePanel *panel) {
  xcb_connection_t *connection = panel->connection;
  xcb_window_t window = panel->window;
  xcb_atom_t windowType = internAtom(connection, "_NET_WM_WINDOW_TYPE");
  xcb_atom_t dock = internAtom(connection, "_NET_WM_WINDOW_TYPE_DOCK");
  xcb_atom_t state = internAtom(connection, "_NET_WM_STATE");
  xcb_atom_t states[] = {internAtom(connection, "_NET_WM_STATE_STICKY"),
                         internAtom(connection, "_NET_WM_STATE_ABOVE")};
  xcb_atom_t utf8 = internAtom(connection, "UTF8_STRING");
  xcb_atom_t netName = internAtom(connection, "_NET_WM_NAME");
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      windowType,
                      XCB_ATOM_ATOM,
                      32,
                      1,
                      &dock);
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      state,
                      XCB_ATOM_ATOM,
                      32,
                      2,
                      states);
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      XCB_ATOM_WM_NAME,
                      XCB_ATOM_STRING,
                      8,
                      (uint32_t)strlen(panel->config.wmName),
                      panel->config.wmName);
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      netName,
                      utf8,
                      8,
                      (uint32_t)strlen(panel->config.wmName),
                      panel->config.wmName);
  const char CLASS_NAME[] = "sliverbar\0Sliverbar\0";
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      XCB_ATOM_WM_CLASS,
                      XCB_ATOM_STRING,
                      8,
                      sizeof(CLASS_NAME) - 1,
                      CLASS_NAME);
  uint32_t strut[12] = {0};
  strut[2] = (uint32_t)(panel->y + panel->config.height);
  strut[8] = (uint32_t)panel->x;
  strut[9] = (uint32_t)(panel->x + panel->width - 1);
  setCardinal(connection,
              window,
              internAtom(connection, "_NET_WM_STRUT_PARTIAL"),
              strut,
              12);
  setCardinal(
      connection, window, internAtom(connection, "_NET_WM_STRUT"), strut, 4);
}

NativePanel *nativePanelCreate(xcb_connection_t *connection,
                               xcb_screen_t *screen,
                               const PanelConfig *config,
                               char *error,
                               size_t errorSize) {
  xcb_visualtype_t *visual = findVisual(screen);
  if (!visual) {
    snprintf(error, errorSize, "cannot find the root X11 visual");
    return NULL;
  }
  NativePanel *panel = calloc(1, sizeof(*panel));
  if (!panel) {
    snprintf(error, errorSize, "cannot allocate the native panel");
    return NULL;
  }
  panel->connection = connection;
  panel->screen = screen;
  panel->config = *config;
  resolveMonitorBounds(panel);
#ifdef HAVE_XCB_RANDR
  subscribeRandr(panel);
#endif
  panel->window = xcb_generate_id(connection);
  uint32_t values[] = {
      screen->black_pixel,
      XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS |
          XCB_EVENT_MASK_STRUCTURE_NOTIFY,
  };
  xcb_create_window(connection,
                    screen->root_depth,
                    panel->window,
                    screen->root,
                    (int16_t)panel->x,
                    (int16_t)panel->y,
                    (uint16_t)panel->width,
                    (uint16_t)config->height,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                    values);
  configureEwmh(panel);
  panel->tray = nativeTrayCreate(
      connection, screen, panel->window, config->height, config->colorBg);
  if (!panel->tray) {
    snprintf(error, errorSize, "cannot allocate the native system tray");
    nativePanelDestroy(panel);
    return NULL;
  }
  panel->surface = cairo_xcb_surface_create(
      connection, panel->window, visual, panel->width, config->height);
  panel->presentCairo = cairo_create(panel->surface);
  if (createBackBuffer(panel)) {
    snprintf(error, errorSize, "cannot create the panel back buffer");
    nativePanelDestroy(panel);
    return NULL;
  }
  panel->layout = pango_cairo_create_layout(panel->cairo);
  char description[256];
  fontName(config->font, description, sizeof(description));
  panel->font = pango_font_description_from_string(description);
  if (config->iconFont[0]) {
    fontName(config->iconFont, description, sizeof(description));
    panel->iconFont = pango_font_description_from_string(description);
  } else {
    panel->iconFont = pango_font_description_copy(panel->font);
  }
  pango_layout_set_font_description(panel->layout, panel->font);
  xcb_flush(connection);
  if (cairo_surface_status(panel->surface) != CAIRO_STATUS_SUCCESS ||
      cairo_status(panel->presentCairo) != CAIRO_STATUS_SUCCESS) {
    snprintf(error, errorSize, "cannot create the Cairo X11 surface");
    nativePanelDestroy(panel);
    return NULL;
  }
  return panel;
}

void nativePanelDestroy(NativePanel *panel) {
  if (!panel)
    return;
  nativeTrayDestroy(panel->tray);
  if (panel->layout)
    g_object_unref(panel->layout);
  if (panel->font)
    pango_font_description_free(panel->font);
  if (panel->iconFont)
    pango_font_description_free(panel->iconFont);
  if (panel->cairo)
    cairo_destroy(panel->cairo);
  if (panel->presentCairo)
    cairo_destroy(panel->presentCairo);
  if (panel->backSurface)
    cairo_surface_destroy(panel->backSurface);
  if (panel->surface)
    cairo_surface_destroy(panel->surface);
  if (panel->window)
    xcb_destroy_window(panel->connection, panel->window);
  xcb_flush(panel->connection);
  free(panel);
}

static void resetStyle(DrawStyle *style, const PanelConfig *config) {
  snprintf(style->foreground, sizeof(style->foreground), "%s", config->colorFg);
  snprintf(
      style->background, sizeof(style->background), "%s", config->colorPanelBg);
  snprintf(style->underlineColor,
           sizeof(style->underlineColor),
           "%s",
           config->colorFg);
  style->underline = false;
}

static void applyCommand(char *command,
                         Alignment *align,
                         DrawStyle *style,
                         ActionEntry *actions,
                         size_t *actionCount,
                         const PanelConfig *config,
                         int *offset,
                         bool *tray) {
  if (!strcmp(command, "l"))
    *align = ALIGN_LEFT;
  else if (!strcmp(command, "c"))
    *align = ALIGN_CENTER;
  else if (!strcmp(command, "r"))
    *align = ALIGN_RIGHT;
  else if (!strcmp(command, "+u"))
    style->underline = true;
  else if (!strcmp(command, "-u"))
    style->underline = false;
  else if (command[0] == 'F')
    snprintf(style->foreground,
             sizeof(style->foreground),
             "%.15s",
             command[1] == '-' ? config->colorFg : command + 1);
  else if (command[0] == 'B')
    snprintf(style->background,
             sizeof(style->background),
             "%.15s",
             command[1] == '-' ? config->colorPanelBg : command + 1);
  else if (command[0] == 'U')
    snprintf(style->underlineColor,
             sizeof(style->underlineColor),
             "%.15s",
             command[1] == '-' ? config->colorFg : command + 1);
  else if (command[0] == 'O')
    *offset = atoi(command + 1);
  else if (!strcmp(command, "t"))
    *tray = true;
  else if (!strcmp(command, "t-"))
    *tray = false;
  else if (!strcmp(command, "A")) {
    if (*actionCount)
      (*actionCount)--;
  } else if (command[0] == 'A' && *actionCount < MAX_ACTION_DEPTH) {
    char *colon = strchr(command, ':');
    char *last = strrchr(command, ':');
    if (!colon || colon == last)
      return;
    *last = '\0';
    actions[*actionCount].button = (uint8_t)atoi(command + 1);
    snprintf(actions[*actionCount].command,
             sizeof(actions[*actionCount].command),
             "%s",
             colon + 1);
    (*actionCount)++;
  }
}

static void
copyActions(Segment *item, const ActionEntry *actions, size_t actionCount) {
  for (size_t i = 0; i < actionCount; i++) {
    uint8_t button = actions[i].button;
    if (button >= 1 && button <= 5)
      snprintf(item->actions[button - 1],
               sizeof(item->actions[button - 1]),
               "%s",
               actions[i].command);
  }
}

static size_t
parseMarkup(NativePanel *panel, const char *markup, Segment *segments) {
  DrawStyle style;
  resetStyle(&style, &panel->config);
  Alignment align = ALIGN_LEFT;
  ActionEntry actions[MAX_ACTION_DEPTH] = {0};
  size_t actionCount = 0, count = 0;
  bool tray = false;
  const char *cursor = markup;
  while (*cursor && count < MAX_SEGMENTS) {
    if (!strncmp(cursor, "%{", 2)) {
      const char *end = strchr(cursor + 2, '}');
      if (!end)
        break;
      char commands[512];
      size_t length = (size_t)(end - cursor - 2);
      if (length >= sizeof(commands))
        length = sizeof(commands) - 1;
      memcpy(commands, cursor + 2, length);
      commands[length] = '\0';
      int offset = 0;
      applyCommand(commands,
                   &align,
                   &style,
                   actions,
                   &actionCount,
                   &panel->config,
                   &offset,
                   &tray);
      if (offset && count < MAX_SEGMENTS) {
        segments[count].align = align;
        segments[count].style = style;
        segments[count].offset = offset;
        segments[count].width = offset;
        segments[count].tray = tray;
        copyActions(&segments[count], actions, actionCount);
        count++;
      }
      cursor = end + 1;
      continue;
    }
    const char *end = strstr(cursor, "%{");
    size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
    while (length && (cursor[length - 1] == '\n' || cursor[length - 1] == '\r'))
      length--;
    if (length) {
      Segment *item = &segments[count++];
      item->align = align;
      item->style = style;
      if (length >= sizeof(item->text))
        length = sizeof(item->text) - 1;
      memcpy(item->text, cursor, length);
      item->text[length] = '\0';
      copyActions(item, actions, actionCount);
    }
    cursor += length;
    if (!end)
      break;
  }
  return count;
}

static bool iconCodepoint(gunichar codepoint) {
  return (codepoint >= 0xe000U && codepoint <= 0xf8ffU) ||
         (codepoint >= 0xf0000U && codepoint <= 0xffffdU) ||
         (codepoint >= 0x100000U && codepoint <= 0x10fffdU);
}

static void prepareLayout(NativePanel *panel, const char *text) {
  pango_layout_set_text(panel->layout, text, -1);
  PangoAttrList *attributes = pango_attr_list_new();
  const char *cursor = text;
  while (*cursor) {
    const char *next = g_utf8_next_char(cursor);
    if (iconCodepoint(g_utf8_get_char(cursor))) {
      PangoAttribute *attribute = pango_attr_font_desc_new(panel->iconFont);
      attribute->start_index = (guint)(cursor - text);
      attribute->end_index = (guint)(next - text);
      pango_attr_list_insert(attributes, attribute);
    }
    cursor = next;
  }
  pango_layout_set_attributes(panel->layout, attributes);
  pango_attr_list_unref(attributes);
}

static int textWidth(NativePanel *panel, const char *text) {
  prepareLayout(panel, text);
  int width = 0;
  pango_layout_get_pixel_size(panel->layout, &width, NULL);
  return width;
}

static void addRegions(NativePanel *panel, const Segment *item, int x) {
  for (uint8_t button = 1; button <= 5; button++) {
    if (!item->actions[button - 1][0])
      continue;
    bool extended = false;
    for (size_t i = panel->regionCount; i > 0; i--) {
      ActionRegion *existing = &panel->regions[i - 1];
      if (existing->button == button && existing->x1 == x &&
          !strcmp(existing->command, item->actions[button - 1])) {
        existing->x1 += item->width;
        extended = true;
        break;
      }
    }
    if (extended || panel->regionCount >= MAX_REGIONS)
      continue;
    ActionRegion *region = &panel->regions[panel->regionCount++];
    region->x0 = x;
    region->x1 = x + item->width;
    region->button = button;
    snprintf(region->command,
             sizeof(region->command),
             "%s",
             item->actions[button - 1]);
  }
}

static void
drawSegmentBackground(NativePanel *panel, const Segment *item, int x) {
  setColor(panel->cairo, item->style.background, panel->config.colorPanelBg);
  cairo_rectangle(panel->cairo, x, 0, item->width, panel->config.height);
  cairo_fill(panel->cairo);
}

static void drawSegmentContent(NativePanel *panel, const Segment *item, int x) {
  if (!item->text[0])
    return;
  prepareLayout(panel, item->text);
  int textHeight = 0;
  pango_layout_get_pixel_size(panel->layout, NULL, &textHeight);
  setColor(panel->cairo, item->style.foreground, panel->config.colorFg);
  cairo_move_to(panel->cairo, x, (panel->config.height - textHeight) / 2.0);
  pango_cairo_show_layout(panel->cairo, panel->layout);
  if (item->style.underline && panel->config.underline > 0) {
    setColor(panel->cairo, item->style.underlineColor, panel->config.colorFg);
    cairo_rectangle(panel->cairo,
                    x,
                    panel->config.height - panel->config.underline,
                    item->width,
                    panel->config.underline);
    cairo_fill(panel->cairo);
  }
}

static int drawMarkup(NativePanel *panel, const char *markup) {
  Segment segments[MAX_SEGMENTS] = {0};
  size_t count = parseMarkup(panel, markup, segments);
  int widths[3] = {0};
  for (size_t i = 0; i < count; i++) {
    if (!segments[i].offset)
      segments[i].width = textWidth(panel, segments[i].text);
    widths[segments[i].align] += segments[i].width;
  }
  int positions[3] = {0,
                      (panel->width - widths[ALIGN_CENTER]) / 2,
                      panel->width - widths[ALIGN_RIGHT]};
  setColor(panel->cairo, panel->config.colorPanelBg, "#000000");
  cairo_paint(panel->cairo);
  panel->regionCount = 0;
  int segmentX[MAX_SEGMENTS] = {0};
  for (size_t i = 0; i < count; i++) {
    int *x = &positions[segments[i].align];
    segmentX[i] = *x;
    drawSegmentBackground(panel, &segments[i], *x);
    if (segments[i].align == ALIGN_RIGHT && segments[i].tray &&
        moduleModeActive(panel->config.moduleTray,
                         nativeTrayAvailable(panel->tray)))
      nativeTrayLayout(panel->tray, *x + 4);
    *x += segments[i].width;
  }
  for (size_t i = 0; i < count; i++) {
    drawSegmentContent(panel, &segments[i], segmentX[i]);
    addRegions(panel, &segments[i], segmentX[i]);
  }
  cairo_surface_flush(panel->backSurface);
  cairo_set_source_surface(panel->presentCairo, panel->backSurface, 0, 0);
  cairo_set_operator(panel->presentCairo, CAIRO_OPERATOR_SOURCE);
  cairo_paint(panel->presentCairo);
  cairo_surface_flush(panel->surface);
  xcb_flush(panel->connection);
  return cairo_status(panel->cairo) == CAIRO_STATUS_SUCCESS &&
                 cairo_status(panel->presentCairo) == CAIRO_STATUS_SUCCESS
             ? 0
             : -1;
}

int nativePanelDraw(NativePanel *panel, const PanelState *state) {
  PanelState rendered = *state;
  char markup[sizeof(panel->lastMarkup)];
  if (moduleModeActive(panel->config.moduleTray,
                       nativeTrayAvailable(panel->tray))) {
    int width = nativeTrayWidth(panel->tray);
    snprintf(rendered.tray,
             sizeof(rendered.tray),
             "%%{F%s}%%{B%s}%%{t}%%{O%d}%%{t-}%%{B-}%%{F-}",
             panel->config.colorFg,
             panel->config.colorBg,
             width + 4);
  }
  renderPanel(&rendered, markup, sizeof(markup));
  if (!panel->repaintRequested && !strcmp(markup, panel->lastMarkup))
    return 0;
  snprintf(panel->lastMarkup, sizeof(panel->lastMarkup), "%s", markup);
  int result = drawMarkup(panel, markup);
  if (!result)
    panel->repaintRequested = false;
  if (!result && moduleModeActive(panel->config.moduleTray,
                                  nativeTrayAvailable(panel->tray)))
    nativeTrayAcquire(panel->tray);
  if (!result && !panel->mapped) {
    xcb_map_window(panel->connection, panel->window);
    xcb_flush(panel->connection);
    panel->mapped = true;
  }
  return result;
}

bool nativePanelHandleEvent(NativePanel *panel,
                            const xcb_generic_event_t *event,
                            char *action,
                            size_t actionSize,
                            bool *redraw) {
  uint8_t type = event->response_type & 0x7fU;
#ifdef HAVE_XCB_RANDR
  if (panel->randrEventBase &&
      (type == panel->randrEventBase + XCB_RANDR_SCREEN_CHANGE_NOTIFY ||
       type == panel->randrEventBase + XCB_RANDR_NOTIFY)) {
    int oldX = panel->x, oldY = panel->y, oldWidth = panel->width;
    resolveMonitorBounds(panel);
    if (panel->x != oldX || panel->y != oldY || panel->width != oldWidth) {
      uint32_t geometry[] = {(uint32_t)panel->x,
                             (uint32_t)panel->y,
                             (uint32_t)panel->width,
                             (uint32_t)panel->config.height};
      xcb_configure_window(panel->connection,
                           panel->window,
                           XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                               XCB_CONFIG_WINDOW_WIDTH |
                               XCB_CONFIG_WINDOW_HEIGHT,
                           geometry);
      cairo_xcb_surface_set_size(
          panel->surface, panel->width, panel->config.height);
      if (createBackBuffer(panel))
        return false;
      configureEwmh(panel);
      *redraw = true;
    }
    return false;
  }
#endif
  if (nativeTrayHandleEvent(panel->tray, event)) {
    panel->repaintRequested = true;
    *redraw = true;
    return false;
  }
  if (type == XCB_EXPOSE) {
    panel->repaintRequested = true;
    *redraw = true;
    return false;
  }
  if (type == XCB_MAP_NOTIFY &&
      ((const xcb_map_notify_event_t *)event)->window == panel->window) {
    nativeTraySetVisible(panel->tray, true);
    return false;
  }
  if (type == XCB_UNMAP_NOTIFY &&
      ((const xcb_unmap_notify_event_t *)event)->window == panel->window) {
    nativeTraySetVisible(panel->tray, false);
    return false;
  }
  if (type != XCB_BUTTON_PRESS)
    return false;
  const xcb_button_press_event_t *button =
      (const xcb_button_press_event_t *)event;
  if (button->event != panel->window)
    return false;
  for (size_t i = panel->regionCount; i > 0; i--) {
    const ActionRegion *region = &panel->regions[i - 1];
    if (region->button == button->detail && button->event_x >= region->x0 &&
        button->event_x < region->x1) {
      snprintf(action, actionSize, "%s", region->command);
      return true;
    }
  }
  return false;
}

bool nativePanelActionBounds(const NativePanel *panel,
                             const char *action,
                             int *x,
                             int *width) {
  if (!panel || !action)
    return false;
  for (size_t i = panel->regionCount; i > 0; i--) {
    const ActionRegion *region = &panel->regions[i - 1];
    if (strcmp(region->command, action) != 0)
      continue;
    if (x)
      *x = panel->x + region->x0;
    if (width)
      *width = region->x1 - region->x0;
    return true;
  }
  return false;
}

xcb_window_t nativePanelWindow(const NativePanel *panel) {
  return panel->window;
}

void nativePanelBounds(
    const NativePanel *panel, int *x, int *y, int *width, int *height) {
  if (!panel)
    return;
  if (x)
    *x = panel->x;
  if (y)
    *y = panel->y;
  if (width)
    *width = panel->width;
  if (height)
    *height = panel->config.height;
}

bool nativePanelOwnsTray(const NativePanel *panel) {
  return panel && nativeTrayOwnsSelection(panel->tray);
}

size_t nativePanelTrayIconCount(const NativePanel *panel) {
  return panel ? nativeTrayIconCount(panel->tray) : 0;
}

xcb_atom_t nativePanelTrayOpcode(const NativePanel *panel) {
  return panel ? nativeTrayOpcode(panel->tray) : XCB_ATOM_NONE;
}

#endif
