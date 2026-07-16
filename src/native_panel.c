#include "native_panel.h"
#include "native_tray.h"

#ifdef HAVE_NATIVE_PANEL

#include <cairo/cairo-xcb.h>
#include <pango/pangocairo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEGMENTS 256
#define MAX_REGIONS 256
#define MAX_ACTION_DEPTH 32

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } alignment;

typedef struct {
    char foreground[16];
    char background[16];
    char underline_color[16];
    bool underline;
} draw_style;

typedef struct {
    alignment align;
    draw_style style;
    char text[512];
    int offset;
    int width;
    char actions[5][256];
} segment;

typedef struct {
    int x0;
    int x1;
    uint8_t button;
    char command[256];
} action_region;

typedef struct {
    uint8_t button;
    char command[256];
} action_entry;

struct native_panel {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    cairo_surface_t *surface;
    cairo_t *cairo;
    PangoLayout *layout;
    PangoFontDescription *font;
    PangoFontDescription *icon_font;
    native_tray *tray;
    panel_config config;
    bool mapped;
    action_region regions[MAX_REGIONS];
    size_t region_count;
    char last_markup[32768];
};

static xcb_atom_t intern_atom(xcb_connection_t *connection, const char *name) {
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);
    xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return result;
}

static xcb_visualtype_t *find_visual(xcb_screen_t *screen) {
    xcb_depth_iterator_t depths = xcb_screen_allowed_depths_iterator(screen);
    for (; depths.rem; xcb_depth_next(&depths)) {
        xcb_visualtype_iterator_t visuals = xcb_depth_visuals_iterator(depths.data);
        for (; visuals.rem; xcb_visualtype_next(&visuals))
            if (visuals.data->visual_id == screen->root_visual)
                return visuals.data;
    }
    return NULL;
}

static bool parse_color(const char *value, double *red, double *green, double *blue) {
    unsigned rgb;
    if (!value || value[0] != '#' || strlen(value) != 7 || sscanf(value + 1, "%06x", &rgb) != 1)
        return false;
    *red = (double)((rgb >> 16) & 0xffU) / 255.0;
    *green = (double)((rgb >> 8) & 0xffU) / 255.0;
    *blue = (double)(rgb & 0xffU) / 255.0;
    return true;
}

static void set_color(cairo_t *cairo, const char *value, const char *fallback) {
    double red = 0.0, green = 0.0, blue = 0.0;
    if (!parse_color(value, &red, &green, &blue))
        parse_color(fallback, &red, &green, &blue);
    cairo_set_source_rgb(cairo, red, green, blue);
}

static void font_name(const char *configured, char *output, size_t size) {
    char copy[128];
    snprintf(copy, sizeof(copy), "%s", configured);
    char *style = strstr(copy, ":style=");
    char *font_size = strstr(copy, ":size=");
    if (style)
        *style = '\0';
    else if (font_size)
        *font_size = '\0';
    const char *style_value = style ? style + 7 : "Regular";
    if (font_size && style && font_size > style)
        *font_size = '\0';
    const char *size_value = font_size ? font_size + 6 : "10";
    snprintf(output, size, "%s %s %s", copy, style_value, size_value);
}

static void set_cardinal(xcb_connection_t *connection,
                         xcb_window_t window,
                         xcb_atom_t property,
                         const uint32_t *values,
                         uint32_t count) {
    xcb_change_property(
        connection, XCB_PROP_MODE_REPLACE, window, property, XCB_ATOM_CARDINAL, 32, count, values);
}

static void configure_ewmh(native_panel *panel) {
    xcb_connection_t *connection = panel->connection;
    xcb_window_t window = panel->window;
    xcb_atom_t window_type = intern_atom(connection, "_NET_WM_WINDOW_TYPE");
    xcb_atom_t dock = intern_atom(connection, "_NET_WM_WINDOW_TYPE_DOCK");
    xcb_atom_t state = intern_atom(connection, "_NET_WM_STATE");
    xcb_atom_t states[] = {intern_atom(connection, "_NET_WM_STATE_STICKY"),
                           intern_atom(connection, "_NET_WM_STATE_ABOVE")};
    xcb_atom_t utf8 = intern_atom(connection, "UTF8_STRING");
    xcb_atom_t net_name = intern_atom(connection, "_NET_WM_NAME");
    xcb_change_property(
        connection, XCB_PROP_MODE_REPLACE, window, window_type, XCB_ATOM_ATOM, 32, 1, &dock);
    xcb_change_property(
        connection, XCB_PROP_MODE_REPLACE, window, state, XCB_ATOM_ATOM, 32, 2, states);
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING,
                        8,
                        (uint32_t)strlen(panel->config.wm_name),
                        panel->config.wm_name);
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        net_name,
                        utf8,
                        8,
                        (uint32_t)strlen(panel->config.wm_name),
                        panel->config.wm_name);
    const char class_name[] = "lemonbar-c\0panel\0";
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        XCB_ATOM_WM_CLASS,
                        XCB_ATOM_STRING,
                        8,
                        sizeof(class_name) - 1,
                        class_name);
    uint32_t strut[12] = {0};
    strut[2] = (uint32_t)panel->config.height;
    strut[8] = 0;
    strut[9] = panel->screen->width_in_pixels - 1U;
    set_cardinal(connection, window, intern_atom(connection, "_NET_WM_STRUT_PARTIAL"), strut, 12);
    set_cardinal(connection, window, intern_atom(connection, "_NET_WM_STRUT"), strut, 4);
}

native_panel *native_panel_create(xcb_connection_t *connection,
                                  xcb_screen_t *screen,
                                  const panel_config *config,
                                  char *error,
                                  size_t error_size) {
    xcb_visualtype_t *visual = find_visual(screen);
    if (!visual) {
        snprintf(error, error_size, "cannot find the root X11 visual");
        return NULL;
    }
    native_panel *panel = calloc(1, sizeof(*panel));
    if (!panel) {
        snprintf(error, error_size, "cannot allocate the native panel");
        return NULL;
    }
    panel->connection = connection;
    panel->screen = screen;
    panel->config = *config;
    panel->window = xcb_generate_id(connection);
    uint32_t values[] = {
        screen->black_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
    };
    xcb_create_window(connection,
                      screen->root_depth,
                      panel->window,
                      screen->root,
                      0,
                      0,
                      screen->width_in_pixels,
                      (uint16_t)config->height,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                      values);
    configure_ewmh(panel);
    panel->tray = native_tray_create(connection, screen, panel->window, config->height);
    if (!panel->tray) {
        snprintf(error, error_size, "cannot allocate the native system tray");
        native_panel_destroy(panel);
        return NULL;
    }
    panel->surface = cairo_xcb_surface_create(
        connection, panel->window, visual, screen->width_in_pixels, config->height);
    panel->cairo = cairo_create(panel->surface);
    panel->layout = pango_cairo_create_layout(panel->cairo);
    char description[256];
    font_name(config->font, description, sizeof(description));
    panel->font = pango_font_description_from_string(description);
    font_name(config->icon_font, description, sizeof(description));
    panel->icon_font = pango_font_description_from_string(description);
    pango_layout_set_font_description(panel->layout, panel->font);
    xcb_flush(connection);
    if (cairo_surface_status(panel->surface) != CAIRO_STATUS_SUCCESS) {
        snprintf(error, error_size, "cannot create the Cairo X11 surface");
        native_panel_destroy(panel);
        return NULL;
    }
    return panel;
}

void native_panel_destroy(native_panel *panel) {
    if (!panel)
        return;
    native_tray_destroy(panel->tray);
    if (panel->layout)
        g_object_unref(panel->layout);
    if (panel->font)
        pango_font_description_free(panel->font);
    if (panel->icon_font)
        pango_font_description_free(panel->icon_font);
    if (panel->cairo)
        cairo_destroy(panel->cairo);
    if (panel->surface)
        cairo_surface_destroy(panel->surface);
    if (panel->window)
        xcb_destroy_window(panel->connection, panel->window);
    xcb_flush(panel->connection);
    free(panel);
}

static void reset_style(draw_style *style, const panel_config *config) {
    snprintf(style->foreground, sizeof(style->foreground), "%s", config->color_fg);
    snprintf(style->background, sizeof(style->background), "%s", config->color_panel_bg);
    snprintf(style->underline_color, sizeof(style->underline_color), "%s", config->color_fg);
    style->underline = false;
}

static void apply_command(char *command,
                          alignment *align,
                          draw_style *style,
                          action_entry *actions,
                          size_t *action_count,
                          const panel_config *config,
                          int *offset) {
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
                 command[1] == '-' ? config->color_fg : command + 1);
    else if (command[0] == 'B')
        snprintf(style->background,
                 sizeof(style->background),
                 "%.15s",
                 command[1] == '-' ? config->color_panel_bg : command + 1);
    else if (command[0] == 'U')
        snprintf(style->underline_color,
                 sizeof(style->underline_color),
                 "%.15s",
                 command[1] == '-' ? config->color_fg : command + 1);
    else if (command[0] == 'O')
        *offset = atoi(command + 1);
    else if (!strcmp(command, "A")) {
        if (*action_count)
            (*action_count)--;
    } else if (command[0] == 'A' && *action_count < MAX_ACTION_DEPTH) {
        char *colon = strchr(command, ':');
        char *last = strrchr(command, ':');
        if (!colon || colon == last)
            return;
        *last = '\0';
        actions[*action_count].button = (uint8_t)atoi(command + 1);
        snprintf(actions[*action_count].command,
                 sizeof(actions[*action_count].command),
                 "%s",
                 colon + 1);
        (*action_count)++;
    }
}

static size_t parse_markup(native_panel *panel, const char *markup, segment *segments) {
    draw_style style;
    reset_style(&style, &panel->config);
    alignment align = ALIGN_LEFT;
    action_entry actions[MAX_ACTION_DEPTH] = {0};
    size_t action_count = 0, count = 0;
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
            apply_command(
                commands, &align, &style, actions, &action_count, &panel->config, &offset);
            if (offset && count < MAX_SEGMENTS) {
                segments[count].align = align;
                segments[count].style = style;
                segments[count].offset = offset;
                segments[count].width = offset;
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
            segment *item = &segments[count++];
            item->align = align;
            item->style = style;
            if (length >= sizeof(item->text))
                length = sizeof(item->text) - 1;
            memcpy(item->text, cursor, length);
            item->text[length] = '\0';
            for (size_t i = 0; i < action_count; i++) {
                uint8_t button = actions[i].button;
                if (button >= 1 && button <= 5)
                    snprintf(item->actions[button - 1],
                             sizeof(item->actions[button - 1]),
                             "%s",
                             actions[i].command);
            }
        }
        cursor += length;
        if (!end)
            break;
    }
    return count;
}

static bool icon_codepoint(gunichar codepoint) {
    return codepoint >= 0xe000U && codepoint <= 0xf8ffU;
}

static void prepare_layout(native_panel *panel, const char *text) {
    pango_layout_set_text(panel->layout, text, -1);
    PangoAttrList *attributes = pango_attr_list_new();
    const char *cursor = text;
    while (*cursor) {
        const char *next = g_utf8_next_char(cursor);
        if (icon_codepoint(g_utf8_get_char(cursor))) {
            PangoAttribute *attribute = pango_attr_font_desc_new(panel->icon_font);
            attribute->start_index = (guint)(cursor - text);
            attribute->end_index = (guint)(next - text);
            pango_attr_list_insert(attributes, attribute);
        }
        cursor = next;
    }
    pango_layout_set_attributes(panel->layout, attributes);
    pango_attr_list_unref(attributes);
}

static int text_width(native_panel *panel, const char *text) {
    prepare_layout(panel, text);
    int width = 0;
    pango_layout_get_pixel_size(panel->layout, &width, NULL);
    return width;
}

static void add_regions(native_panel *panel, const segment *item, int x) {
    for (uint8_t button = 1; button <= 5 && panel->region_count < MAX_REGIONS; button++) {
        if (!item->actions[button - 1][0])
            continue;
        action_region *region = &panel->regions[panel->region_count++];
        region->x0 = x;
        region->x1 = x + item->width;
        region->button = button;
        snprintf(region->command, sizeof(region->command), "%s", item->actions[button - 1]);
    }
}

static void draw_segment(native_panel *panel, const segment *item, int x) {
    set_color(panel->cairo, item->style.background, panel->config.color_panel_bg);
    cairo_rectangle(panel->cairo, x, 0, item->width, panel->config.height);
    cairo_fill(panel->cairo);
    if (!item->text[0])
        return;
    prepare_layout(panel, item->text);
    int text_height = 0;
    pango_layout_get_pixel_size(panel->layout, NULL, &text_height);
    set_color(panel->cairo, item->style.foreground, panel->config.color_fg);
    cairo_move_to(panel->cairo, x, (panel->config.height - text_height) / 2.0);
    pango_cairo_show_layout(panel->cairo, panel->layout);
    if (item->style.underline && panel->config.underline > 0) {
        set_color(panel->cairo, item->style.underline_color, panel->config.color_fg);
        cairo_rectangle(panel->cairo,
                        x,
                        panel->config.height - panel->config.underline,
                        item->width,
                        panel->config.underline);
        cairo_fill(panel->cairo);
    }
}

static int draw_markup(native_panel *panel, const char *markup) {
    segment segments[MAX_SEGMENTS] = {0};
    size_t count = parse_markup(panel, markup, segments);
    int widths[3] = {0};
    for (size_t i = 0; i < count; i++) {
        if (!segments[i].offset)
            segments[i].width = text_width(panel, segments[i].text);
        widths[segments[i].align] += segments[i].width;
    }
    int positions[3] = {0,
                        ((int)panel->screen->width_in_pixels - widths[ALIGN_CENTER]) / 2,
                        (int)panel->screen->width_in_pixels - widths[ALIGN_RIGHT]};
    set_color(panel->cairo, panel->config.color_panel_bg, "#000000");
    cairo_paint(panel->cairo);
    panel->region_count = 0;
    for (size_t i = 0; i < count; i++) {
        int *x = &positions[segments[i].align];
        draw_segment(panel, &segments[i], *x);
        if (segments[i].align == ALIGN_RIGHT && segments[i].offset &&
            native_tray_owns_selection(panel->tray))
            native_tray_layout(panel->tray, *x + 4);
        add_regions(panel, &segments[i], *x);
        *x += segments[i].width;
    }
    cairo_surface_flush(panel->surface);
    xcb_flush(panel->connection);
    return cairo_status(panel->cairo) == CAIRO_STATUS_SUCCESS ? 0 : -1;
}

int native_panel_draw(native_panel *panel, const panel_state *state) {
    panel_state rendered = *state;
    if (native_tray_owns_selection(panel->tray)) {
        int width = native_tray_width(panel->tray);
        if (width > 0)
            snprintf(rendered.tray,
                     sizeof(rendered.tray),
                     "%%{F%s}%%{B%s}%%{O%d}%%{B-}%%{F-}",
                     panel->config.color_fg,
                     panel->config.color_bg,
                     width + 4);
        else
            rendered.tray[0] = '\0';
    }
    render_panel(&rendered, panel->last_markup, sizeof(panel->last_markup));
    int result = draw_markup(panel, panel->last_markup);
    if (!result && !panel->mapped) {
        xcb_map_window(panel->connection, panel->window);
        xcb_flush(panel->connection);
        panel->mapped = true;
    }
    return result;
}

bool native_panel_handle_event(native_panel *panel,
                               const xcb_generic_event_t *event,
                               char *action,
                               size_t action_size,
                               bool *redraw) {
    uint8_t type = event->response_type & 0x7fU;
    if (native_tray_handle_event(panel->tray, event)) {
        *redraw = true;
        return false;
    }
    if (type == XCB_EXPOSE) {
        *redraw = true;
        return false;
    }
    if (type != XCB_BUTTON_PRESS)
        return false;
    const xcb_button_press_event_t *button = (const xcb_button_press_event_t *)event;
    if (button->event != panel->window)
        return false;
    for (size_t i = panel->region_count; i > 0; i--) {
        const action_region *region = &panel->regions[i - 1];
        if (region->button == button->detail && button->event_x >= region->x0 &&
            button->event_x < region->x1) {
            snprintf(action, action_size, "%s", region->command);
            return true;
        }
    }
    return false;
}

xcb_window_t native_panel_window(const native_panel *panel) {
    return panel->window;
}

bool native_panel_owns_tray(const native_panel *panel) {
    return panel && native_tray_owns_selection(panel->tray);
}

size_t native_panel_tray_icon_count(const native_panel *panel) {
    return panel ? native_tray_icon_count(panel->tray) : 0;
}

xcb_atom_t native_panel_tray_opcode(const native_panel *panel) {
    return panel ? native_tray_opcode(panel->tray) : XCB_ATOM_NONE;
}

#endif
