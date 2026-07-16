#ifndef LEMONBAR_C_NATIVE_TRAY_H
#define LEMONBAR_C_NATIVE_TRAY_H

#include <stdbool.h>
#include <stddef.h>

#include <xcb/xcb.h>

typedef struct native_tray native_tray;

native_tray *native_tray_create(xcb_connection_t *connection,
                                xcb_screen_t *screen,
                                xcb_window_t panel_window,
                                int panel_height);
void native_tray_destroy(native_tray *tray);
bool native_tray_acquire(native_tray *tray);
bool native_tray_handle_event(native_tray *tray, const xcb_generic_event_t *event);
void native_tray_layout(native_tray *tray, int x);
void native_tray_set_visible(native_tray *tray, bool visible);
bool native_tray_owns_selection(const native_tray *tray);
bool native_tray_available(const native_tray *tray);
size_t native_tray_icon_count(const native_tray *tray);
int native_tray_width(const native_tray *tray);
xcb_atom_t native_tray_opcode(const native_tray *tray);

#endif
