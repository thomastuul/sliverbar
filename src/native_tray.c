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
#define XEMBED_MAPPED (1U << 0)

typedef struct {
    xcb_window_t window;
    int width;
    bool mapped;
} tray_icon;

struct native_tray {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t panel_window;
    xcb_atom_t selection;
    xcb_atom_t opcode;
    xcb_atom_t manager;
    xcb_atom_t xembed;
    xcb_atom_t xembed_info;
    int panel_height;
    int icon_height;
    int gap;
    int layout_x;
    bool owns_selection;
    tray_icon icons[MAX_TRAY_ICONS];
    size_t icon_count;
};

static xcb_atom_t intern_atom(xcb_connection_t *connection, const char *name) {
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);
    xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return result;
}

static bool has_icon(const native_tray *tray, xcb_window_t window) {
    for (size_t i = 0; i < tray->icon_count; i++)
        if (tray->icons[i].window == window)
            return true;
    return false;
}

static bool icon_mapped(native_tray *tray, xcb_window_t window) {
    xcb_get_property_cookie_t cookie =
        xcb_get_property(tray->connection, 0, window, tray->xembed_info, tray->xembed_info, 0, 2);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(tray->connection, cookie, NULL);
    bool mapped = true;
    if (reply && xcb_get_property_value_length(reply) >= (int)(2 * sizeof(uint32_t))) {
        const uint32_t *values = xcb_get_property_value(reply);
        mapped = (values[1] & XEMBED_MAPPED) != 0;
    }
    free(reply);
    return mapped;
}

static void send_xembed(native_tray *tray, xcb_window_t window) {
    xcb_client_message_event_t message = {0};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = window;
    message.type = tray->xembed;
    message.data.data32[0] = XCB_CURRENT_TIME;
    message.data.data32[1] = XEMBED_EMBEDDED_NOTIFY;
    message.data.data32[2] = 0;
    message.data.data32[3] = tray->panel_window;
    message.data.data32[4] = 0;
    xcb_send_event(tray->connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (const char *)&message);
}

static int scaled_width(native_tray *tray, xcb_window_t window) {
    xcb_get_geometry_reply_t *geometry =
        xcb_get_geometry_reply(tray->connection, xcb_get_geometry(tray->connection, window), NULL);
    int width = tray->icon_height;
    if (geometry && geometry->height > 0) {
        int64_t scaled = (int64_t)geometry->width * tray->icon_height / geometry->height;
        if (scaled < tray->icon_height)
            scaled = tray->icon_height;
        int64_t maximum = (int64_t)tray->icon_height * 2;
        if (scaled > maximum)
            scaled = maximum;
        width = (int)scaled;
    }
    free(geometry);
    return width;
}

static bool dock_icon(native_tray *tray, xcb_window_t window) {
    if (!tray->owns_selection || window == XCB_WINDOW_NONE || has_icon(tray, window) ||
        tray->icon_count >= MAX_TRAY_ICONS)
        return false;
    tray_icon *icon = &tray->icons[tray->icon_count++];
    icon->window = window;
    icon->width = scaled_width(tray, window);
    icon->mapped = icon_mapped(tray, window);
    uint32_t event_mask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(tray->connection, window, XCB_CW_EVENT_MASK, &event_mask);
    xcb_change_save_set(tray->connection, XCB_SET_MODE_INSERT, window);
    xcb_reparent_window(tray->connection, window, tray->panel_window, 0, 0);
    uint32_t values[] = {0, 0, (uint32_t)icon->width, (uint32_t)tray->icon_height, 0};
    xcb_configure_window(tray->connection,
                         window,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                             XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
    send_xembed(tray, window);
    if (icon->mapped)
        xcb_map_window(tray->connection, window);
    xcb_flush(tray->connection);
    return true;
}

static bool remove_icon(native_tray *tray, xcb_window_t window, bool reparent) {
    for (size_t i = 0; i < tray->icon_count; i++) {
        if (tray->icons[i].window != window)
            continue;
        if (reparent) {
            xcb_unmap_window(tray->connection, window);
            xcb_reparent_window(tray->connection, window, tray->screen->root, 0, 0);
            xcb_change_save_set(tray->connection, XCB_SET_MODE_DELETE, window);
        }
        memmove(&tray->icons[i],
                &tray->icons[i + 1],
                (tray->icon_count - i - 1) * sizeof(tray->icons[0]));
        tray->icon_count--;
        xcb_flush(tray->connection);
        return true;
    }
    return false;
}

native_tray *native_tray_create(xcb_connection_t *connection,
                                xcb_screen_t *screen,
                                xcb_window_t panel_window,
                                int panel_height) {
    native_tray *tray = calloc(1, sizeof(*tray));
    if (!tray)
        return NULL;
    tray->connection = connection;
    tray->screen = screen;
    tray->panel_window = panel_window;
    tray->panel_height = panel_height;
    tray->icon_height = panel_height > 2 ? panel_height - 2 : panel_height;
    tray->gap = 1;
    tray->selection = intern_atom(connection, "_NET_SYSTEM_TRAY_S0");
    tray->opcode = intern_atom(connection, "_NET_SYSTEM_TRAY_OPCODE");
    tray->manager = intern_atom(connection, "MANAGER");
    tray->xembed = intern_atom(connection, "_XEMBED");
    tray->xembed_info = intern_atom(connection, "_XEMBED_INFO");
    xcb_get_selection_owner_reply_t *owner = xcb_get_selection_owner_reply(
        connection, xcb_get_selection_owner(connection, tray->selection), NULL);
    bool available = owner && owner->owner == XCB_WINDOW_NONE;
    free(owner);
    if (!available) {
        log_message("WARNING", "another system tray manager already owns _NET_SYSTEM_TRAY_S0");
        return tray;
    }
    uint32_t orientation = 0;
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        panel_window,
                        intern_atom(connection, "_NET_SYSTEM_TRAY_ORIENTATION"),
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        &orientation);
    xcb_atom_t visual_id = intern_atom(connection, "VISUALID");
    uint32_t visual = screen->root_visual;
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        panel_window,
                        intern_atom(connection, "_NET_SYSTEM_TRAY_VISUAL"),
                        visual_id,
                        32,
                        1,
                        &visual);
    xcb_set_selection_owner(connection, panel_window, tray->selection, XCB_CURRENT_TIME);
    xcb_flush(connection);
    owner = xcb_get_selection_owner_reply(
        connection, xcb_get_selection_owner(connection, tray->selection), NULL);
    tray->owns_selection = owner && owner->owner == panel_window;
    free(owner);
    if (!tray->owns_selection)
        return tray;
    xcb_client_message_event_t manager = {0};
    manager.response_type = XCB_CLIENT_MESSAGE;
    manager.format = 32;
    manager.window = screen->root;
    manager.type = tray->manager;
    manager.data.data32[0] = XCB_CURRENT_TIME;
    manager.data.data32[1] = tray->selection;
    manager.data.data32[2] = panel_window;
    xcb_send_event(
        connection, 0, screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&manager);
    xcb_flush(connection);
    return tray;
}

void native_tray_destroy(native_tray *tray) {
    if (!tray)
        return;
    while (tray->icon_count)
        remove_icon(tray, tray->icons[tray->icon_count - 1].window, true);
    if (tray->owns_selection)
        xcb_set_selection_owner(
            tray->connection, XCB_WINDOW_NONE, tray->selection, XCB_CURRENT_TIME);
    xcb_flush(tray->connection);
    free(tray);
}

bool native_tray_handle_event(native_tray *tray, const xcb_generic_event_t *event) {
    if (!tray || !tray->owns_selection)
        return false;
    uint8_t type = event->response_type & 0x7fU;
    if (type == XCB_CLIENT_MESSAGE) {
        const xcb_client_message_event_t *message = (const xcb_client_message_event_t *)event;
        if (message->type == tray->opcode && message->format == 32 &&
            message->data.data32[1] == SYSTEM_TRAY_REQUEST_DOCK)
            return dock_icon(tray, message->data.data32[2]);
    } else if (type == XCB_DESTROY_NOTIFY) {
        return remove_icon(tray, ((const xcb_destroy_notify_event_t *)event)->window, false);
    } else if (type == XCB_REPARENT_NOTIFY) {
        const xcb_reparent_notify_event_t *reparent = (const xcb_reparent_notify_event_t *)event;
        if (reparent->parent != tray->panel_window)
            return remove_icon(tray, reparent->window, false);
    } else if (type == XCB_PROPERTY_NOTIFY) {
        const xcb_property_notify_event_t *property = (const xcb_property_notify_event_t *)event;
        if (property->atom == tray->xembed_info && has_icon(tray, property->window)) {
            bool mapped = icon_mapped(tray, property->window);
            for (size_t i = 0; i < tray->icon_count; i++) {
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
        const xcb_configure_request_event_t *request = (const xcb_configure_request_event_t *)event;
        if (has_icon(tray, request->window)) {
            native_tray_layout(tray, tray->layout_x);
            return true;
        }
    } else if (type == XCB_SELECTION_CLEAR) {
        const xcb_selection_clear_event_t *clear = (const xcb_selection_clear_event_t *)event;
        if (clear->selection == tray->selection) {
            while (tray->icon_count)
                remove_icon(tray, tray->icons[tray->icon_count - 1].window, true);
            tray->owns_selection = false;
            log_message("ERROR", "native system tray selection was lost");
            return true;
        }
    }
    return false;
}

void native_tray_layout(native_tray *tray, int x) {
    if (!tray || !tray->owns_selection)
        return;
    tray->layout_x = x;
    int cursor = x;
    for (size_t i = 0; i < tray->icon_count; i++) {
        tray_icon *icon = &tray->icons[i];
        uint32_t values[] = {(uint32_t)cursor,
                             (uint32_t)((tray->panel_height - tray->icon_height) / 2),
                             (uint32_t)icon->width,
                             (uint32_t)tray->icon_height,
                             0};
        xcb_configure_window(tray->connection,
                             icon->window,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                 XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
                             values);
        if (icon->mapped)
            xcb_map_window(tray->connection, icon->window);
        cursor += icon->width + tray->gap;
    }
    xcb_flush(tray->connection);
}

bool native_tray_owns_selection(const native_tray *tray) {
    return tray && tray->owns_selection;
}

size_t native_tray_icon_count(const native_tray *tray) {
    return tray ? tray->icon_count : 0;
}

int native_tray_width(const native_tray *tray) {
    if (!tray || !tray->owns_selection || !tray->icon_count)
        return 0;
    int width = 0;
    for (size_t i = 0; i < tray->icon_count; i++)
        width += tray->icons[i].width;
    if (tray->icon_count > 1 && tray->icon_count - 1 <= (size_t)INT_MAX)
        width += (int)(tray->icon_count - 1) * tray->gap;
    return width;
}

xcb_atom_t native_tray_opcode(const native_tray *tray) {
    return tray ? tray->opcode : XCB_ATOM_NONE;
}
