#include "native_tray.h"

#include "panel.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRAY_ICONS 64
#define SYSTEM_TRAY_REQUEST_DOCK 0
#define XEMBED_EMBEDDED_NOTIFY 0
#define XEMBED_WINDOW_ACTIVATE 1
#define XEMBED_MAPPED (1U << 0)

typedef struct {
  xcb_window_t window;
  xcb_window_t container;
  int width;
  bool mapped;
  bool embedded;
} TrayIcon;

struct NativeTray {
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t panelWindow;
  xcb_window_t hostWindow;
  xcb_atom_t selection;
  xcb_atom_t opcode;
  xcb_atom_t manager;
  xcb_atom_t xembed;
  xcb_atom_t xembedInfo;
  int panelHeight;
  int iconHeight;
  int gap;
  int layoutX;
  int layoutRight;
  bool available;
  bool ownsSelection;
  bool visible;
  TrayIcon icons[MAX_TRAY_ICONS];
  size_t iconCount;
};

static xcb_atom_t internAtom(xcb_connection_t *connection, const char *name) {
  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
  xcb_intern_atom_reply_t *reply =
      xcb_intern_atom_reply(connection, cookie, NULL);
  xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
  free(reply);
  return result;
}

static bool hasIcon(const NativeTray *tray, xcb_window_t window) {
  for (size_t i = 0; i < tray->iconCount; i++)
    if (tray->icons[i].window == window)
      return true;
  return false;
}

static bool iconMapped(NativeTray *tray, xcb_window_t window) {
  xcb_get_property_cookie_t cookie = xcb_get_property(
      tray->connection, 0, window, tray->xembedInfo, tray->xembedInfo, 0, 2);
  xcb_get_property_reply_t *reply =
      xcb_get_property_reply(tray->connection, cookie, NULL);
  bool mapped = true;
  if (reply &&
      xcb_get_property_value_length(reply) >= (int)(2 * sizeof(uint32_t))) {
    const uint32_t *values = xcb_get_property_value(reply);
    mapped = (values[1] & XEMBED_MAPPED) != 0;
  }
  free(reply);
  return mapped;
}

static void sendXembed(NativeTray *tray,
                       const TrayIcon *icon,
                       uint32_t opcode,
                       uint32_t data1) {
  xcb_client_message_event_t message = {0};
  message.response_type = XCB_CLIENT_MESSAGE;
  message.format = 32;
  message.window = icon->window;
  message.type = tray->xembed;
  message.data.data32[0] = XCB_CURRENT_TIME;
  message.data.data32[1] = opcode;
  message.data.data32[2] = 0;
  message.data.data32[3] = data1;
  message.data.data32[4] = 0;
  xcb_send_event(tray->connection,
                 0,
                 icon->window,
                 XCB_EVENT_MASK_NO_EVENT,
                 (const char *)&message);
}

static int scaledWidth(NativeTray *tray, xcb_window_t window) {
  xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
      tray->connection, xcb_get_geometry(tray->connection, window), NULL);
  int width = tray->iconHeight;
  if (geometry && geometry->height > 0) {
    int64_t scaled =
        (int64_t)geometry->width * tray->iconHeight / geometry->height;
    if (scaled < tray->iconHeight)
      scaled = tray->iconHeight;
    int64_t maximum = (int64_t)tray->iconHeight * 2;
    if (scaled > maximum)
      scaled = maximum;
    width = (int)scaled;
  }
  free(geometry);
  return width;
}

static void sendConfigure(NativeTray *tray, const TrayIcon *icon) {
  xcb_configure_notify_event_t configured = {0};
  configured.response_type = XCB_CONFIGURE_NOTIFY;
  configured.event = icon->window;
  configured.window = icon->window;
  configured.x = 0;
  configured.y = 0;
  configured.width = (uint16_t)icon->width;
  configured.height = (uint16_t)tray->iconHeight;
  configured.border_width = 0;
  configured.above_sibling = XCB_WINDOW_NONE;
  configured.override_redirect = 0;
  xcb_send_event(tray->connection,
                 0,
                 icon->window,
                 XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                 (const char *)&configured);
}

static bool dockIcon(NativeTray *tray, xcb_window_t window) {
  if (!tray->ownsSelection || window == XCB_WINDOW_NONE ||
      hasIcon(tray, window) || tray->iconCount >= MAX_TRAY_ICONS)
    return false;
  TrayIcon *icon = &tray->icons[tray->iconCount++];
  icon->window = window;
  icon->width = scaledWidth(tray, window);
  icon->mapped = iconMapped(tray, window);
  icon->container = xcb_generate_id(tray->connection);
  uint32_t containerValues[] = {XCB_BACK_PIXMAP_PARENT_RELATIVE,
                                XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                                    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY};
  xcb_create_window(tray->connection,
                    tray->screen->root_depth,
                    icon->container,
                    tray->hostWindow,
                    0,
                    0,
                    (uint16_t)icon->width,
                    (uint16_t)tray->iconHeight,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    tray->screen->root_visual,
                    XCB_CW_BACK_PIXMAP | XCB_CW_EVENT_MASK,
                    containerValues);
  uint32_t eventMask =
      XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
  xcb_change_window_attributes(
      tray->connection, window, XCB_CW_EVENT_MASK, &eventMask);
  xcb_change_save_set(tray->connection, XCB_SET_MODE_INSERT, window);
  xcb_reparent_window(tray->connection, window, icon->container, 0, 0);
  int width = nativeTrayWidth(tray);
  nativeTrayLayout(tray, tray->layoutRight - width);
  sendXembed(tray, icon, XEMBED_EMBEDDED_NOTIFY, icon->container);
  icon->embedded = true;
  nativeTrayLayout(tray, tray->layoutX);
  sendXembed(tray, icon, XEMBED_WINDOW_ACTIVATE, 0);
  xcb_flush(tray->connection);
  return true;
}

static bool removeIcon(NativeTray *tray, xcb_window_t window, bool reparent) {
  for (size_t i = 0; i < tray->iconCount; i++) {
    if (tray->icons[i].window != window)
      continue;
    if (reparent) {
      xcb_unmap_window(tray->connection, window);
      xcb_reparent_window(tray->connection, window, tray->screen->root, 0, 0);
      xcb_change_save_set(tray->connection, XCB_SET_MODE_DELETE, window);
    }
    xcb_destroy_window(tray->connection, tray->icons[i].container);
    memmove(&tray->icons[i],
            &tray->icons[i + 1],
            (tray->iconCount - i - 1) * sizeof(tray->icons[0]));
    tray->iconCount--;
    xcb_flush(tray->connection);
    return true;
  }
  return false;
}

NativeTray *nativeTrayCreate(xcb_connection_t *connection,
                             xcb_screen_t *screen,
                             xcb_window_t panelWindow,
                             int panelHeight) {
  NativeTray *tray = calloc(1, sizeof(*tray));
  if (!tray)
    return NULL;
  tray->connection = connection;
  tray->screen = screen;
  tray->panelWindow = panelWindow;
  tray->panelHeight = panelHeight;
  tray->iconHeight = panelHeight > 2 ? panelHeight - 2 : panelHeight;
  tray->gap = 1;
  tray->visible = true;
  tray->selection = internAtom(connection, "_NET_SYSTEM_TRAY_S0");
  tray->opcode = internAtom(connection, "_NET_SYSTEM_TRAY_OPCODE");
  tray->manager = internAtom(connection, "MANAGER");
  tray->xembed = internAtom(connection, "_XEMBED");
  tray->xembedInfo = internAtom(connection, "_XEMBED_INFO");
  tray->hostWindow = xcb_generate_id(connection);
  uint32_t hostValues[] = {screen->black_pixel,
                           1,
                           XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                               XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY};
  xcb_create_window(connection,
                    screen->root_depth,
                    tray->hostWindow,
                    screen->root,
                    0,
                    0,
                    1,
                    (uint16_t)panelHeight,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
                        XCB_CW_EVENT_MASK,
                    hostValues);
  static const char WINDOW_CLASS[] = "lemonbar-c\0lemonbar-c";
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      tray->hostWindow,
                      XCB_ATOM_WM_CLASS,
                      XCB_ATOM_STRING,
                      8,
                      sizeof(WINDOW_CLASS),
                      WINDOW_CLASS);
  xcb_get_selection_owner_reply_t *owner = xcb_get_selection_owner_reply(
      connection, xcb_get_selection_owner(connection, tray->selection), NULL);
  bool available = owner && owner->owner == XCB_WINDOW_NONE;
  free(owner);
  if (!available) {
    logMessage("WARNING",
               "another system tray manager already owns _NET_SYSTEM_TRAY_S0");
    return tray;
  }
  tray->available = true;
  return tray;
}

bool nativeTrayAcquire(NativeTray *tray) {
  if (!tray || !tray->available)
    return false;
  if (tray->ownsSelection)
    return true;
  uint32_t orientation = 0;
  xcb_change_property(
      tray->connection,
      XCB_PROP_MODE_REPLACE,
      tray->hostWindow,
      internAtom(tray->connection, "_NET_SYSTEM_TRAY_ORIENTATION"),
      XCB_ATOM_CARDINAL,
      32,
      1,
      &orientation);
  xcb_atom_t visualId = internAtom(tray->connection, "VISUALID");
  uint32_t visual = tray->screen->root_visual;
  xcb_change_property(tray->connection,
                      XCB_PROP_MODE_REPLACE,
                      tray->hostWindow,
                      internAtom(tray->connection, "_NET_SYSTEM_TRAY_VISUAL"),
                      visualId,
                      32,
                      1,
                      &visual);
  xcb_set_selection_owner(
      tray->connection, tray->hostWindow, tray->selection, XCB_CURRENT_TIME);
  xcb_flush(tray->connection);
  xcb_get_selection_owner_reply_t *owner = xcb_get_selection_owner_reply(
      tray->connection,
      xcb_get_selection_owner(tray->connection, tray->selection),
      NULL);
  tray->ownsSelection = owner && owner->owner == tray->hostWindow;
  free(owner);
  if (!tray->ownsSelection)
    return false;
  xcb_client_message_event_t manager = {0};
  manager.response_type = XCB_CLIENT_MESSAGE;
  manager.format = 32;
  manager.window = tray->screen->root;
  manager.type = tray->manager;
  manager.data.data32[0] = XCB_CURRENT_TIME;
  manager.data.data32[1] = tray->selection;
  manager.data.data32[2] = tray->hostWindow;
  xcb_send_event(tray->connection,
                 0,
                 tray->screen->root,
                 XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                 (const char *)&manager);
  xcb_flush(tray->connection);
  return true;
}

void nativeTrayDestroy(NativeTray *tray) {
  if (!tray)
    return;
  while (tray->iconCount)
    removeIcon(tray, tray->icons[tray->iconCount - 1].window, true);
  if (tray->ownsSelection)
    xcb_set_selection_owner(
        tray->connection, XCB_WINDOW_NONE, tray->selection, XCB_CURRENT_TIME);
  if (tray->hostWindow)
    xcb_destroy_window(tray->connection, tray->hostWindow);
  xcb_flush(tray->connection);
  free(tray);
}

bool nativeTrayHandleEvent(NativeTray *tray, const xcb_generic_event_t *event) {
  if (!tray || !tray->ownsSelection)
    return false;
  uint8_t type = event->response_type & 0x7fU;
  if (type == XCB_CLIENT_MESSAGE) {
    const xcb_client_message_event_t *message =
        (const xcb_client_message_event_t *)event;
    if (message->type == tray->opcode && message->format == 32 &&
        message->data.data32[1] == SYSTEM_TRAY_REQUEST_DOCK)
      return dockIcon(tray, message->data.data32[2]);
  } else if (type == XCB_DESTROY_NOTIFY) {
    return removeIcon(
        tray, ((const xcb_destroy_notify_event_t *)event)->window, false);
  } else if (type == XCB_REPARENT_NOTIFY) {
    const xcb_reparent_notify_event_t *reparent =
        (const xcb_reparent_notify_event_t *)event;
    for (size_t i = 0; i < tray->iconCount; i++)
      if (tray->icons[i].window == reparent->window &&
          reparent->parent != tray->icons[i].container)
        return removeIcon(tray, reparent->window, false);
  } else if (type == XCB_PROPERTY_NOTIFY) {
    const xcb_property_notify_event_t *property =
        (const xcb_property_notify_event_t *)event;
    if (property->atom == tray->xembedInfo && hasIcon(tray, property->window)) {
      bool mapped = iconMapped(tray, property->window);
      for (size_t i = 0; i < tray->iconCount; i++) {
        if (tray->icons[i].window != property->window)
          continue;
        tray->icons[i].mapped = mapped;
        if (mapped)
          xcb_map_window(tray->connection, property->window);
        else
          xcb_unmap_window(tray->connection, property->window);
        xcb_flush(tray->connection);
        return true;
      }
    }
  } else if (type == XCB_CONFIGURE_REQUEST) {
    const xcb_configure_request_event_t *request =
        (const xcb_configure_request_event_t *)event;
    if (hasIcon(tray, request->window)) {
      nativeTrayLayout(tray, tray->layoutX);
      return true;
    }
  } else if (type == XCB_SELECTION_CLEAR) {
    const xcb_selection_clear_event_t *clear =
        (const xcb_selection_clear_event_t *)event;
    if (clear->selection == tray->selection) {
      while (tray->iconCount)
        removeIcon(tray, tray->icons[tray->iconCount - 1].window, true);
      tray->ownsSelection = false;
      logMessage("ERROR", "native system tray selection was lost");
      return true;
    }
  }
  return false;
}

void nativeTrayLayout(NativeTray *tray, int x) {
  if (!tray || !tray->available)
    return;
  tray->layoutX = x;
  int width = nativeTrayWidth(tray);
  tray->layoutRight = x + width;
  uint32_t hostValues[] = {(uint32_t)x,
                           0,
                           (uint32_t)(width > 0 ? width : 1),
                           (uint32_t)tray->panelHeight,
                           0};
  xcb_configure_window(tray->connection,
                       tray->hostWindow,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_BORDER_WIDTH,
                       hostValues);
  if (tray->visible)
    xcb_map_window(tray->connection, tray->hostWindow);
  int cursor = 0;
  for (size_t remaining = tray->iconCount; remaining > 0; remaining--) {
    TrayIcon *icon = &tray->icons[remaining - 1];
    uint32_t values[] = {(uint32_t)cursor,
                         (uint32_t)((tray->panelHeight - tray->iconHeight) / 2),
                         (uint32_t)icon->width,
                         (uint32_t)tray->iconHeight,
                         0};
    xcb_configure_window(
        tray->connection,
        icon->container,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
        values);
    xcb_map_window(tray->connection, icon->container);
    if (!icon->embedded) {
      cursor += icon->width + tray->gap;
      continue;
    }
    uint32_t clientValues[] = {
        0, 0, (uint32_t)icon->width, (uint32_t)tray->iconHeight, 0};
    xcb_configure_window(
        tray->connection,
        icon->window,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
        clientValues);
    sendConfigure(tray, icon);
    if (icon->mapped)
      xcb_map_window(tray->connection, icon->window);
    cursor += icon->width + tray->gap;
  }
  xcb_flush(tray->connection);
}

void nativeTraySetVisible(NativeTray *tray, bool visible) {
  if (!tray || tray->visible == visible)
    return;
  tray->visible = visible;
  if (visible)
    xcb_map_window(tray->connection, tray->hostWindow);
  else
    xcb_unmap_window(tray->connection, tray->hostWindow);
  xcb_flush(tray->connection);
}

bool nativeTrayOwnsSelection(const NativeTray *tray) {
  return tray && tray->ownsSelection;
}

bool nativeTrayAvailable(const NativeTray *tray) {
  return tray && tray->available;
}

size_t nativeTrayIconCount(const NativeTray *tray) {
  return tray ? tray->iconCount : 0;
}

int nativeTrayWidth(const NativeTray *tray) {
  if (!tray || !tray->ownsSelection || !tray->iconCount)
    return 0;
  int width = 0;
  for (size_t i = 0; i < tray->iconCount; i++)
    width += tray->icons[i].width;
  if (tray->iconCount > 1 && tray->iconCount - 1 <= (size_t)INT_MAX)
    width += (int)(tray->iconCount - 1) * tray->gap;
  return width;
}

xcb_atom_t nativeTrayOpcode(const NativeTray *tray) {
  return tray ? tray->opcode : XCB_ATOM_NONE;
}
