#ifndef SLIVERBAR_NATIVE_PANEL_H
#define SLIVERBAR_NATIVE_PANEL_H

#include "panel.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef HAVE_NATIVE_PANEL
#include <xcb/xcb.h>

typedef struct NativePanel NativePanel;

NativePanel *nativePanelCreate(xcb_connection_t *connection,
                               xcb_screen_t *screen,
                               const PanelConfig *config,
                               char *error,
                               size_t errorSize);
void nativePanelDestroy(NativePanel *panel);
int nativePanelDraw(NativePanel *panel, const PanelState *state);
void nativePanelBounds(
    const NativePanel *panel, int *x, int *y, int *width, int *height);
bool nativePanelHandleEvent(NativePanel *panel,
                            const xcb_generic_event_t *event,
                            char *action,
                            size_t actionSize,
                            bool *redraw);
xcb_window_t nativePanelWindow(const NativePanel *panel);
bool nativePanelOwnsTray(const NativePanel *panel);
size_t nativePanelTrayIconCount(const NativePanel *panel);
xcb_atom_t nativePanelTrayOpcode(const NativePanel *panel);

#endif

#endif
