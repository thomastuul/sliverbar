#ifndef LEMONBAR_C_NATIVE_TRAY_H
#define LEMONBAR_C_NATIVE_TRAY_H

#include <stdbool.h>
#include <stddef.h>

#include <xcb/xcb.h>

typedef struct NativeTray NativeTray;

NativeTray *nativeTrayCreate(xcb_connection_t *connection,
                             xcb_screen_t *screen,
                             xcb_window_t panelWindow,
                             int panelHeight);
void nativeTrayDestroy(NativeTray *tray);
bool nativeTrayAcquire(NativeTray *tray);
bool nativeTrayHandleEvent(NativeTray *tray, const xcb_generic_event_t *event);
void nativeTrayLayout(NativeTray *tray, int x);
void nativeTraySetVisible(NativeTray *tray, bool visible);
bool nativeTrayOwnsSelection(const NativeTray *tray);
bool nativeTrayAvailable(const NativeTray *tray);
size_t nativeTrayIconCount(const NativeTray *tray);
int nativeTrayWidth(const NativeTray *tray);
xcb_atom_t nativeTrayOpcode(const NativeTray *tray);

#endif
