#ifndef SLIVERBAR_NATIVE_POPUP_H
#define SLIVERBAR_NATIVE_POPUP_H

#include "agenda.h"
#include "panel.h"
#include "weather_forecast.h"

#ifdef HAVE_NATIVE_PANEL

#include <stdbool.h>
#include <stddef.h>
#include <xcb/xcb.h>

#define POPUP_ITEM_MAX 512

typedef struct {
  char label[160];
  char search[512];
  char action[256];
} PopupItem;

typedef struct NativePopup NativePopup;

NativePopup *nativePopupCreate(xcb_connection_t *connection,
                               xcb_screen_t *screen,
                               const PanelConfig *config);
void nativePopupDestroy(NativePopup *popup);
bool nativePopupAvailable(const NativePopup *popup);
bool nativePopupIsOpen(const NativePopup *popup);
void nativePopupSetBounds(
    NativePopup *popup, int x, int y, int width, int panelHeight);
int nativePopupOpen(NativePopup *popup,
                    const PopupItem *items,
                    size_t count,
                    bool searchable,
                    bool anchorRight);
int nativePopupOpenAt(NativePopup *popup,
                      const PopupItem *items,
                      size_t count,
                      bool searchable,
                      int actionX,
                      int actionWidth);
int nativePopupOpenForecast(NativePopup *popup,
                            const WeatherForecast *forecast,
                            const char *location,
                            int actionX,
                            int actionWidth);
void nativePopupUpdateForecast(NativePopup *popup,
                               const WeatherForecast *forecast,
                               const char *location);
bool nativePopupIsForecastOpen(const NativePopup *popup);
int nativePopupOpenAgenda(NativePopup *popup,
                          const AgendaView *agenda,
                          int actionX,
                          int actionWidth);
void nativePopupUpdateAgenda(NativePopup *popup, const AgendaView *agenda);
bool nativePopupIsAgendaOpen(const NativePopup *popup);
void nativePopupGeometry(
    const NativePopup *popup, int *x, int *y, int *width, int *height);
void nativePopupClose(NativePopup *popup);
bool nativePopupHandleEvent(NativePopup *popup,
                            const xcb_generic_event_t *event,
                            char *action,
                            size_t actionSize,
                            bool *redraw);

#endif

#endif
