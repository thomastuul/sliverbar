#ifndef LEMONBAR_C_NATIVE_PANEL_H
#define LEMONBAR_C_NATIVE_PANEL_H

#include "panel.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef HAVE_NATIVE_PANEL
#include <xcb/xcb.h>

typedef struct native_panel native_panel;

native_panel *native_panel_create(xcb_connection_t *connection,
                                  xcb_screen_t *screen,
                                  const panel_config *config,
                                  char *error,
                                  size_t error_size);
void native_panel_destroy(native_panel *panel);
int native_panel_draw(native_panel *panel, const panel_state *state);
bool native_panel_handle_event(native_panel *panel,
                               const xcb_generic_event_t *event,
                               char *action,
                               size_t action_size,
                               bool *redraw);
xcb_window_t native_panel_window(const native_panel *panel);
bool native_panel_owns_tray(const native_panel *panel);
size_t native_panel_tray_icon_count(const native_panel *panel);
xcb_atom_t native_panel_tray_opcode(const native_panel *panel);

#endif

#endif
