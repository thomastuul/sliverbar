#include "native_tray.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition);  \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

static xcb_atom_t atom(xcb_connection_t *connection, const char *name) {
  xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
      connection,
      xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name),
      NULL);
  CHECK(reply != NULL);
  xcb_atom_t result = reply->atom;
  free(reply);
  return result;
}

static void syncConnection(xcb_connection_t *connection) {
  xcb_get_input_focus_reply_t *reply = xcb_get_input_focus_reply(
      connection, xcb_get_input_focus(connection), NULL);
  CHECK(reply != NULL);
  free(reply);
}

static void setMapped(xcb_connection_t *connection,
                      xcb_window_t window,
                      xcb_atom_t xembedInfo,
                      bool mapped) {
  uint32_t values[] = {1, mapped ? 1U : 0U};
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      xembedInfo,
                      xembedInfo,
                      32,
                      2,
                      values);
  syncConnection(connection);
}

static xcb_window_t parentOf(xcb_connection_t *connection,
                             xcb_window_t window) {
  xcb_query_tree_reply_t *reply = xcb_query_tree_reply(
      connection, xcb_query_tree(connection, window), NULL);
  CHECK(reply != NULL);
  xcb_window_t parent = reply->parent;
  free(reply);
  return parent;
}

static void checkWindow(xcb_connection_t *connection,
                        xcb_window_t window,
                        int expectedX,
                        int expectedWidth,
                        bool mapped) {
  xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
      connection, xcb_get_geometry(connection, window), NULL);
  CHECK(geometry != NULL);
  CHECK(geometry->x == expectedX);
  CHECK(geometry->width == expectedWidth);
  free(geometry);
  xcb_get_window_attributes_reply_t *attributes =
      xcb_get_window_attributes_reply(
          connection, xcb_get_window_attributes(connection, window), NULL);
  CHECK(attributes != NULL);
  CHECK(attributes->map_state ==
        (mapped ? XCB_MAP_STATE_VIEWABLE : XCB_MAP_STATE_UNMAPPED));
  free(attributes);
}

static void
propertyChanged(NativeTray *tray, xcb_window_t window, xcb_atom_t xembedInfo) {
  xcb_property_notify_event_t event = {0};
  event.response_type = XCB_PROPERTY_NOTIFY;
  event.window = window;
  event.atom = xembedInfo;
  CHECK(nativeTrayHandleEvent(tray, (const xcb_generic_event_t *)&event));
}

int main(void) {
  xcb_connection_t *manager = xcb_connect(NULL, NULL);
  xcb_connection_t *client = xcb_connect(NULL, NULL);
  CHECK(!xcb_connection_has_error(manager));
  CHECK(!xcb_connection_has_error(client));
  xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(manager)).data;
  NativeTray *tray =
      nativeTrayCreate(manager, screen, screen->root, 30, "#191A21");
  CHECK(tray != NULL);
  CHECK(nativeTrayAcquire(tray));
  xcb_atom_t xembedInfo = atom(client, "_XEMBED_INFO");
  xcb_window_t icons[3], containers[3];
  for (size_t i = 0; i < 3; i++) {
    icons[i] = xcb_generate_id(client);
    xcb_create_window(client,
                      screen->root_depth,
                      icons[i],
                      screen->root,
                      0,
                      0,
                      16,
                      16,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      0,
                      NULL);
    setMapped(client, icons[i], xembedInfo, i != 2);
    xcb_client_message_event_t dock = {0};
    dock.response_type = XCB_CLIENT_MESSAGE;
    dock.type = nativeTrayOpcode(tray);
    dock.format = 32;
    dock.data.data32[2] = icons[i];
    CHECK(nativeTrayHandleEvent(tray, (const xcb_generic_event_t *)&dock));
    containers[i] = parentOf(manager, icons[i]);
  }
  xcb_window_t host = parentOf(manager, containers[0]);
  CHECK(nativeTrayIconCount(tray) == 3);
  CHECK(nativeTrayWidth(tray) == 57);
  nativeTrayLayout(tray, 100);
  checkWindow(manager, host, 100, 57, true);
  checkWindow(manager, containers[1], 0, 28, true);
  checkWindow(manager, containers[0], 29, 28, true);
  checkWindow(manager, icons[2], 0, 16, false);

  /* Recover a visibility update even if its PropertyNotify was missed. */
  setMapped(client, icons[2], xembedInfo, true);
  CHECK(nativeTrayRefresh(tray));
  CHECK(!nativeTrayRefresh(tray));
  nativeTrayLayout(tray, 100);
  CHECK(nativeTrayWidth(tray) == 86);
  checkWindow(manager, icons[2], 0, 28, true);
  checkWindow(manager, containers[2], 0, 28, true);
  checkWindow(manager, containers[1], 29, 28, true);
  checkWindow(manager, containers[0], 58, 28, true);

  /* Hidden middle and leading icons must not leave empty slots or gaps. */
  setMapped(client, icons[1], xembedInfo, false);
  propertyChanged(tray, icons[1], xembedInfo);
  nativeTrayLayout(tray, 100);
  CHECK(nativeTrayWidth(tray) == 57);
  checkWindow(manager, containers[1], 29, 28, false);
  checkWindow(manager, containers[0], 29, 28, true);
  setMapped(client, icons[2], xembedInfo, false);
  propertyChanged(tray, icons[2], xembedInfo);
  nativeTrayLayout(tray, 100);
  CHECK(nativeTrayWidth(tray) == 28);
  checkWindow(manager, host, 100, 28, true);
  checkWindow(manager, containers[0], 0, 28, true);

  setMapped(client, icons[0], xembedInfo, false);
  propertyChanged(tray, icons[0], xembedInfo);
  nativeTrayLayout(tray, 100);
  CHECK(nativeTrayWidth(tray) == 0);
  checkWindow(manager, host, 100, 1, true);
  setMapped(client, icons[2], xembedInfo, true);
  propertyChanged(tray, icons[2], xembedInfo);
  nativeTrayLayout(tray, 100);
  CHECK(nativeTrayWidth(tray) == 28);
  checkWindow(manager, containers[2], 0, 28, true);

  /* An unexpected UnmapNotify must request recovery, not remove the icon. */
  xcb_unmap_window(client, icons[2]);
  syncConnection(client);
  xcb_unmap_notify_event_t unmapped = {0};
  unmapped.response_type = XCB_UNMAP_NOTIFY;
  unmapped.window = icons[2];
  CHECK(nativeTrayHandleEvent(tray, (const xcb_generic_event_t *)&unmapped));
  nativeTrayLayout(tray, 100);
  checkWindow(manager, icons[2], 0, 28, true);
  CHECK(nativeTrayIconCount(tray) == 3);

  nativeTrayDestroy(tray);
  syncConnection(manager);
  xcb_disconnect(client);
  xcb_disconnect(manager);
  puts("native tray visibility checks passed");
  return 0;
}
