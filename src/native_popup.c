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

struct NativePopup {
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t window;
  cairo_surface_t *surface;
  cairo_t *cairo;
  PangoLayout *layout;
  PangoFontDescription *font;
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
  bool open;
  bool searchable;
  char query[256];
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

static void drawText(
    NativePopup *popup, const char *text, int x, int y, const char *color) {
  setColor(popup->cairo, color, "#ffffff");
  pango_layout_set_text(popup->layout, text, -1);
  cairo_move_to(popup->cairo, x, y);
  pango_cairo_show_layout(popup->cairo, popup->layout);
}

static void drawPopup(NativePopup *popup) {
  if (!popup->open)
    return;
  setColor(popup->cairo, popup->config.colorPanelBg, "#000000");
  cairo_paint(popup->cairo);
  int y = 0;
  if (popup->searchable) {
    setColor(popup->cairo, popup->config.colorBg, "#222222");
    cairo_rectangle(popup->cairo, 0, 0, popup->width, popup->rowHeight);
    cairo_fill(popup->cairo);
    char prompt[320];
    snprintf(prompt, sizeof(prompt), "Search: %s", popup->query);
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
    drawText(popup, "No matches", 10, y + 5, popup->config.colorMuted);
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

int nativePopupOpen(NativePopup *popup,
                    const PopupItem *items,
                    size_t count,
                    bool searchable,
                    bool anchorRight) {
  if (!nativePopupAvailable(popup) || !items || count == 0)
    return -1;
  if (count > POPUP_ITEM_MAX)
    count = POPUP_ITEM_MAX;
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
  xcb_map_window(popup->connection, popup->window);
  xcb_flush(popup->connection);
  xcb_get_input_focus_reply_t *focusReply = xcb_get_input_focus_reply(
      popup->connection, xcb_get_input_focus(popup->connection), NULL);
  free(focusReply);
  xcb_set_input_focus(popup->connection,
                      XCB_INPUT_FOCUS_POINTER_ROOT,
                      popup->window,
                      XCB_CURRENT_TIME);
  xcb_grab_keyboard_cookie_t keyboardCookie =
      xcb_grab_keyboard(popup->connection,
                        0,
                        popup->window,
                        XCB_CURRENT_TIME,
                        XCB_GRAB_MODE_ASYNC,
                        XCB_GRAB_MODE_ASYNC);
  xcb_grab_keyboard_reply_t *keyboardReply =
      xcb_grab_keyboard_reply(popup->connection, keyboardCookie, NULL);
  xcb_grab_pointer_cookie_t pointerCookie =
      xcb_grab_pointer(popup->connection,
                       0,
                       popup->window,
                       XCB_EVENT_MASK_BUTTON_PRESS,
                       XCB_GRAB_MODE_ASYNC,
                       XCB_GRAB_MODE_ASYNC,
                       XCB_WINDOW_NONE,
                       XCB_CURSOR_NONE,
                       XCB_CURRENT_TIME);
  xcb_grab_pointer_reply_t *pointerReply =
      xcb_grab_pointer_reply(popup->connection, pointerCookie, NULL);
  bool grabbed = keyboardReply && pointerReply &&
                 keyboardReply->status == XCB_GRAB_STATUS_SUCCESS &&
                 pointerReply->status == XCB_GRAB_STATUS_SUCCESS;
  if (!grabbed)
    logMessage("ERROR",
               "popup input grab failed (keyboard=%d pointer=%d)",
               keyboardReply ? keyboardReply->status : -1,
               pointerReply ? pointerReply->status : -1);
  free(pointerReply);
  free(keyboardReply);
  if (!grabbed) {
    xcb_ungrab_keyboard(popup->connection, XCB_CURRENT_TIME);
    xcb_ungrab_pointer(popup->connection, XCB_CURRENT_TIME);
    xcb_unmap_window(popup->connection, popup->window);
    xcb_flush(popup->connection);
    return -1;
  }
  popup->open = true;
  drawPopup(popup);
  return 0;
}

void nativePopupClose(NativePopup *popup) {
  if (!popup || !popup->open)
    return;
  xcb_ungrab_keyboard(popup->connection, XCB_CURRENT_TIME);
  xcb_ungrab_pointer(popup->connection, XCB_CURRENT_TIME);
  xcb_unmap_window(popup->connection, popup->window);
  xcb_flush(popup->connection);
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
    nativePopupClose(popup);
    return true;
  }
  return false;
}

#endif
