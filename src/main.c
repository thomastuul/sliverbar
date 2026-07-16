#include "native_panel.h"
#include "panel.h"
#include "version.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_XCB
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#endif

static int join_path(char *out, size_t size, const char *base, const char *suffix) {
    size_t a = strlen(base), b = strlen(suffix);
    if (a + b + 1 > size)
        return -1;
    memcpy(out, base, a);
    memcpy(out + a, suffix, b + 1);
    return 0;
}

#ifdef HAVE_NATIVE_PANEL
typedef struct {
    pid_t pid;
    int read_fd, write_fd;
} child;

static int child_pipe(char *const argv[], bool input, child *out) {
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK))
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (!pid) {
        if (input)
            dup2(p[0], STDIN_FILENO);
        else
            dup2(p[1], STDOUT_FILENO);
        close(p[0]);
        close(p[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    out->pid = pid;
    if (input) {
        close(p[0]);
        out->write_fd = p[1];
        out->read_fd = -1;
    } else {
        close(p[1]);
        out->read_fd = p[0];
        out->write_fd = -1;
    }
    return 0;
}

static void stop_child(child *c) {
    if (c->pid > 0) {
        kill(c->pid, SIGTERM);
        for (int i = 0; i < 20 && waitpid(c->pid, NULL, WNOHANG) == 0; i++)
            usleep(10000);
        if (waitpid(c->pid, NULL, WNOHANG) == 0) {
            kill(c->pid, SIGKILL);
            waitpid(c->pid, NULL, 0);
        }
    }
    if (c->read_fd >= 0)
        close(c->read_fd);
    if (c->write_fd >= 0)
        close(c->write_fd);
    memset(c, 0, sizeof(*c));
    c->read_fd = c->write_fd = -1;
}

#ifndef HAVE_XCB
static void retire_child(child *c) {
    if (c->pid > 0)
        kill(c->pid, SIGTERM);
    if (c->read_fd >= 0)
        close(c->read_fd);
    if (c->write_fd >= 0)
        close(c->write_fd);
    c->pid = 0;
    c->read_fd = c->write_fd = -1;
}
#endif

static void store_title(const char *title, unsigned max, panel_state *s, const panel_config *c) {
    char clipped[512], safe[512];
    snprintf(clipped, sizeof(clipped), "%.*s", (int)max, title ? title : "");
    shell_quote_action(clipped, safe, sizeof(safe));
    snprintf(s->title,
             sizeof(s->title),
             "%%{B%s}%%{F%s}%%{+u} %s %%{-u}%%{F-}%%{B-}",
             c->color_bg,
             c->color_free,
             safe);
    if (getenv("LEMONBAR_C_DEBUG"))
        log_message("DEBUG", "title=%s", clipped);
}

#ifdef HAVE_XCB
static xcb_atom_t atom(xcb_connection_t *x, const char *name) {
    xcb_intern_atom_cookie_t ck = xcb_intern_atom(x, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(x, ck, NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}
static void update_title_xcb(xcb_connection_t *x,
                             xcb_window_t root,
                             xcb_atom_t active,
                             xcb_atom_t utf8,
                             xcb_atom_t net_name,
                             unsigned max,
                             panel_state *s,
                             const panel_config *c) {
    xcb_get_property_reply_t *ar = xcb_get_property_reply(
        x, xcb_get_property(x, 0, root, active, XCB_ATOM_WINDOW, 0, 1), NULL);
    if (!ar || xcb_get_property_value_length(ar) < 4) {
        free(ar);
        store_title("", max, s, c);
        return;
    }
    xcb_window_t win = *(xcb_window_t *)xcb_get_property_value(ar);
    free(ar);
    if (win == XCB_WINDOW_NONE) {
        store_title("", max, s, c);
        return;
    }
    uint32_t events = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(x, win, XCB_CW_EVENT_MASK, &events);
    xcb_flush(x);
    xcb_get_property_reply_t *r =
        xcb_get_property_reply(x, xcb_get_property(x, 0, win, net_name, utf8, 0, 1024), NULL);
    if (!r || xcb_get_property_value_length(r) <= 0) {
        free(r);
        r = xcb_get_property_reply(
            x, xcb_get_property(x, 0, win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 1024), NULL);
    }
    if (!r || xcb_get_property_value_length(r) <= 0) {
        free(r);
        store_title("", max, s, c);
        return;
    }
    int len = xcb_get_property_value_length(r);
    char title[512];
    if ((size_t)len >= sizeof(title))
        len = (int)sizeof(title) - 1;
    memcpy(title, xcb_get_property_value(r), (size_t)len);
    title[len] = '\0';
    free(r);
    store_title(title, max, s, c);
}
#else
static int active_window_id(char *id, size_t size) {
    char out[2048];
    char *active[] = {"xprop", "-root", "_NET_ACTIVE_WINDOW", NULL};
    if (run_capture(active, out, sizeof(out), 500))
        return -1;
    char *hash = strchr(out, '#');
    if (!hash)
        return -1;
    hash++;
    while (isspace((unsigned char)*hash))
        hash++;
    snprintf(id, size, "%.24s", hash);
    char *space = strpbrk(id, " \t\r\n");
    if (space)
        *space = '\0';
    return !*id || !strcmp(id, "0x0") ? -1 : 0;
}

static void update_title_for_id(char *id, unsigned max, panel_state *s, const panel_config *c) {
    char out[2048];
    char *name[] = {"xprop", "-id", id, "_NET_WM_NAME", "WM_NAME", NULL};
    if (run_capture(name, out, sizeof(out), 500))
        return;
    char *first = strchr(out, '\"');
    if (first) {
        char *last = strchr(first + 1, '\"');
        if (last) {
            *last = '\0';
            store_title(first + 1, max, s, c);
            return;
        }
    }
    store_title("", max, s, c);
}

static void update_title_fallback(unsigned max, panel_state *s, const panel_config *c) {
    char id[32];
    if (active_window_id(id, sizeof(id))) {
        store_title("", max, s, c);
        return;
    }
    update_title_for_id(id, max, s, c);
}

static int start_window_title_watcher_for_id(child *watcher, char *id) {
    if (command_exists("stdbuf")) {
        char *args[] = {
            "stdbuf", "-oL", "xprop", "-spy", "-id", id, "_NET_WM_NAME", "WM_NAME", NULL};
        return child_pipe(args, false, watcher);
    }
    char *args[] = {"xprop", "-spy", "-id", id, "_NET_WM_NAME", "WM_NAME", NULL};
    return child_pipe(args, false, watcher);
}

static int start_window_title_watcher(child *watcher) {
    char id[32];
    return active_window_id(id, sizeof(id)) ? -1 : start_window_title_watcher_for_id(watcher, id);
}

static int start_active_window_watcher(child *watcher) {
    if (command_exists("stdbuf")) {
        char *args[] = {"stdbuf", "-oL", "xprop", "-spy", "-root", "_NET_ACTIVE_WINDOW", NULL};
        return child_pipe(args, false, watcher);
    }
    char *args[] = {"xprop", "-spy", "-root", "_NET_ACTIVE_WINDOW", NULL};
    return child_pipe(args, false, watcher);
}

static int active_id_from_event(char *event, char *id, size_t size) {
    char *hash = strrchr(event, '#');
    if (!hash)
        return -1;
    hash++;
    while (isspace((unsigned char)*hash))
        hash++;
    snprintf(id, size, "%.24s", hash);
    char *space = strpbrk(id, " \t\r\n");
    if (space)
        *space = '\0';
    return !*id || !strcmp(id, "0x0") ? -1 : 0;
}

static void title_from_event(char *event, unsigned max, panel_state *s, const panel_config *c) {
    char *property = NULL;
    for (char *next = event; (next = strstr(next, "_NET_WM_NAME")); next++)
        property = next;
    if (!property)
        property = event;
    char *first = strchr(property, '\"');
    if (!first) {
        store_title("", max, s, c);
        return;
    }
    char *last = strchr(first + 1, '\"');
    if (!last) {
        store_title("", max, s, c);
        return;
    }
    *last = '\0';
    store_title(first + 1, max, s, c);
}
#endif

static int set_volume(const panel_config *c, const char *op) {
    char step[24], ignored[256];
    snprintf(step, sizeof(step), "%d%%", c->volume_step);
    if (command_exists("pactl")) {
        char value[32];
        if (!strcmp(op, "up"))
            snprintf(value, sizeof(value), "+%s", step);
        else if (!strcmp(op, "down"))
            snprintf(value, sizeof(value), "-%s", step);
        else {
            char *av[] = {"pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle", NULL};
            return run_capture(av, ignored, sizeof(ignored), 1500);
        }
        char *av[] = {"pactl", "set-sink-volume", "@DEFAULT_SINK@", value, NULL};
        return run_capture(av, ignored, sizeof(ignored), 1500);
    }
    char value[32];
    snprintf(value,
             sizeof(value),
             !strcmp(op, "up")     ? "%d%%+"
             : !strcmp(op, "down") ? "%d%%-"
                                   : "toggle",
             c->volume_step);
    char *av[] = {"amixer", "set", "Master", value, NULL};
    return run_capture(av, ignored, sizeof(ignored), 1500);
}

static int set_brightness(const panel_config *c, const char *op) {
    char query[8192];
    char *qv[] = {"xrandr", "--query", NULL};
    if (run_capture(qv, query, sizeof(query), 1200))
        return -1;
    char output[64] = "";
    char *save = NULL;
    for (char *line = strtok_r(query, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char state[32], geometry[64];
        if (sscanf(line, "%63s %31s %63s", output, state, geometry) == 3 &&
            !strcmp(state, "connected") && strchr(geometry, '+'))
            break;
        output[0] = '\0';
    }
    if (!*output)
        return -1;
    char verbose[16384];
    char *vv[] = {"xrandr", "--verbose", "--current", NULL};
    if (run_capture(vv, verbose, sizeof(verbose), 1500))
        return -1;
    char *section = strstr(verbose, output), *p = section ? strstr(section, "Brightness:") : NULL;
    if (!p)
        return -1;
    double current = strtod(p + 11, NULL), delta = (double)c->brightness_step / 100.0,
           target = current + (!strcmp(op, "up") ? delta : -delta);
    if (target < 0.05)
        target = 0.05;
    if (target > 1.0)
        target = 1.0;
    char value[32];
    snprintf(value, sizeof(value), "%.2f", target);
    char *outv[] = {"xrandr", "--output", output, "--brightness", value, NULL};
    char ignored[128];
    return run_capture(outv, ignored, sizeof(ignored), 1500);
}

static void refresh_weather(const panel_config *c) {
    if (!*c->weather_cache || !command_exists("curl"))
        return;
    char location[256], url[512];
    snprintf(location, sizeof(location), "%s", c->location);
    for (char *p = location; *p; p++)
        if (*p == ' ')
            *p = '+';
    snprintf(url, sizeof(url), "https://wttr.in/%s?format=j1&lang=%s", location, c->language);
    char data[32768];
    char *av[] = {"curl", "-fsSL", "--connect-timeout", "3", "--max-time", "15", url, NULL};
    if (!run_capture(av, data, sizeof(data), 18000) && *data) {
        char parent[PANEL_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", c->weather_cache);
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            mkdir_p(parent, 0700);
        }
        if (write_atomic(c->weather_cache, data, 0600))
            log_message("ERROR", "cannot publish weather cache: %s", strerror(errno));
    }
}

static void refresh_weather_image(const panel_config *c) {
    if (!*c->weather_image || !command_exists("curl"))
        return;
    char location[256], url[512], tmp[PANEL_PATH_MAX], parent[PANEL_PATH_MAX];
    snprintf(location, sizeof(location), "%s", c->location);
    for (char *p = location; *p; p++)
        if (*p == ' ')
            *p = '+';
    snprintf(url, sizeof(url), "https://v2.wttr.in/%s.png?lang=%s&m&2", location, c->language);
    if (join_path(tmp, sizeof(tmp), c->weather_image, ".tmp"))
        return;
    snprintf(parent, sizeof(parent), "%s", c->weather_image);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        mkdir_p(parent, 0700);
    }
    char ignored[256];
    char *av[] = {
        "curl", "-fsSL", "--connect-timeout", "3", "--max-time", "15", "-o", tmp, url, NULL};
    if (!run_capture(av, ignored, sizeof(ignored), 18000))
        rename(tmp, c->weather_image);
    else
        unlink(tmp);
}

static pid_t start_weather_refresh(const panel_config *c) {
    pid_t pid = fork();
    if (pid == 0) {
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, NULL);
        refresh_weather(c);
        refresh_weather_image(c);
        _exit(0);
    }
    return pid;
}

static void
do_action(const panel_config *c, const char *line, bool *volume_dirty, bool *workspace_dirty) {
    char copybuf[1024];
    snprintf(copybuf, sizeof(copybuf), "%s", line);
    char *nl = strpbrk(copybuf, "\r\n");
    if (nl)
        *nl = '\0';
    char *save = NULL, *kind = strtok_r(copybuf, "|", &save), *arg = strtok_r(NULL, "|", &save);
    if (!kind)
        return;
    if (getenv("LEMONBAR_C_DEBUG"))
        log_message("DEBUG", "action=%s arg=%s", kind, arg ? arg : "");
    if (!strcmp(kind, "volume") && arg) {
        set_volume(c, arg);
        *volume_dirty = true;
    } else if (!strcmp(kind, "workspace") && arg) {
        char *av[] = {"bspc", "desktop", "-f", arg, NULL};
        char out[64];
        run_capture(av, out, sizeof(out), 1000);
        *workspace_dirty = true;
    } else if (!strcmp(kind, "terminal") && arg) {
        char *av[] = {(char *)c->terminal, "-e", arg, NULL};
        spawn_detached(av);
    } else if (!strcmp(kind, "launcher") && *c->launcher) {
        char *av[] = {(char *)c->launcher, NULL};
        spawn_detached(av);
    } else if (!strcmp(kind, "power") && *c->power_menu) {
        char *av[] = {(char *)c->power_menu, NULL};
        spawn_detached(av);
    } else if (!strcmp(kind, "notify") && arg) {
        char *message = strtok_r(NULL, "", &save);
        char *av[] = {"notify-send", arg, message ? message : "", NULL};
        spawn_detached(av);
    } else if (!strcmp(kind, "weather") && arg && !strcmp(arg, "open") && *c->weather_image) {
        char *av[] = {"xdg-open", (char *)c->weather_image, NULL};
        spawn_detached(av);
    } else if (!strcmp(kind, "weather") && arg && !strcmp(arg, "notify")) {
        char summary[128];
        snprintf(summary, sizeof(summary), "Weather: %.118s", c->location);
        char *av[] = {"notify-send", summary, "Left-click opens the three-day forecast.", NULL};
        spawn_detached(av);
    } else if (!strcmp(kind, "brightness") && arg) {
        if (!set_brightness(c, arg))
            *workspace_dirty = true;
    }
}
#endif

static void usage(FILE *f, const char *name) {
    fprintf(f, "Usage: %s [--config PATH] [--check-config] [--smoke-test] [--version]\n", name);
}

#ifdef HAVE_NATIVE_PANEL
static int
smoke_test_native_tray(native_panel *panel, xcb_connection_t *connection, xcb_screen_t *screen) {
    if (!native_panel_owns_tray(panel)) {
        log_message("ERROR", "native smoke-test could not own the system tray selection");
        return -1;
    }
    xcb_window_t icon = xcb_generate_id(connection);
    uint32_t values[] = {screen->black_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    xcb_create_window(connection,
                      screen->root_depth,
                      icon,
                      screen->root,
                      0,
                      0,
                      16,
                      16,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                      values);
    xcb_client_message_event_t dock = {0};
    dock.response_type = XCB_CLIENT_MESSAGE;
    dock.format = 32;
    dock.window = native_panel_window(panel);
    dock.type = native_panel_tray_opcode(panel);
    dock.data.data32[1] = 0;
    dock.data.data32[2] = icon;
    char action[16] = "";
    bool redraw = false;
    native_panel_handle_event(
        panel, (const xcb_generic_event_t *)&dock, action, sizeof(action), &redraw);
    xcb_query_tree_reply_t *tree =
        xcb_query_tree_reply(connection, xcb_query_tree(connection, icon), NULL);
    bool embedded = tree && tree->parent == native_panel_window(panel) &&
                    native_panel_tray_icon_count(panel) == 1 && redraw;
    free(tree);
    if (!embedded) {
        log_message("ERROR", "native smoke-test tray docking failed");
        xcb_destroy_window(connection, icon);
        return -1;
    }
    xcb_destroy_notify_event_t destroyed = {0};
    destroyed.response_type = XCB_DESTROY_NOTIFY;
    destroyed.window = icon;
    native_panel_handle_event(
        panel, (const xcb_generic_event_t *)&destroyed, action, sizeof(action), &redraw);
    xcb_destroy_window(connection, icon);
    xcb_flush(connection);
    if (native_panel_tray_icon_count(panel) != 0) {
        log_message("ERROR", "native smoke-test tray cleanup failed");
        return -1;
    }
    return 0;
}
#endif

int main(int argc, char **argv) {
    panel_config cfg;
    config_defaults(&cfg);
    const char *config = NULL;
    bool check = false, smoke_test = false;
    signal(SIGPIPE, SIG_IGN);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc)
            config = argv[++i];
        else if (!strcmp(argv[i], "--check-config"))
            check = true;
        else if (!strcmp(argv[i], "--smoke-test"))
            smoke_test = true;
        else if (!strcmp(argv[i], "--version")) {
            puts("lemonbar-panel " LEMONBAR_C_VERSION);
            return 0;
        } else {
            usage(stderr, argv[0]);
            return 2;
        }
    }
    if (!config) {
        config = getenv("LEMONBAR_C_CONFIG");
        if (!config)
            config = "panel.conf";
    }
    char error[512];
    if (config_load(&cfg, config, error, sizeof(error))) {
        log_message("ERROR", "%s", error);
        return 1;
    }
    const char *home = getenv("HOME");
    const char *cache = getenv("XDG_CACHE_HOME");
    char cache_default[PANEL_PATH_MAX], suffix[512];
    if (!cache && home && !join_path(cache_default, sizeof(cache_default), home, "/.cache"))
        cache = cache_default;
    if (home && !*cfg.launcher)
        join_path(
            cfg.launcher, sizeof(cfg.launcher), home, "/.config/bspwm/rofi/launcher/launcher.sh");
    if (home && !*cfg.power_menu)
        join_path(cfg.power_menu,
                  sizeof(cfg.power_menu),
                  home,
                  "/.config/bspwm/rofi/powermenu/powermenu.sh");
    if (cache && !*cfg.weather_cache) {
        snprintf(suffix, sizeof(suffix), "/weather/%s.json", cfg.location);
        join_path(cfg.weather_cache, sizeof(cfg.weather_cache), cache, suffix);
    }
    if (cache && !*cfg.weather_image) {
        snprintf(suffix, sizeof(suffix), "/weather/%s_3days.png", cfg.location);
        join_path(cfg.weather_image, sizeof(cfg.weather_image), cache, suffix);
    }
    if (check) {
        puts("configuration valid");
        return 0;
    }
#ifndef HAVE_NATIVE_PANEL
    (void)smoke_test;
    log_message("ERROR", "native X11 support was unavailable when this binary was built");
    return 1;
#else
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char fallback[64];
    if (!runtime) {
        snprintf(fallback, sizeof(fallback), "/tmp/lemonbar-c-%ld", (long)getuid());
        runtime = fallback;
    }
    char dir[PANEL_PATH_MAX], lockpath[PANEL_PATH_MAX];
    if (join_path(dir, sizeof(dir), runtime, "/lemonbar-c") ||
        join_path(lockpath, sizeof(lockpath), dir, "/panel.lock")) {
        log_message("ERROR", "runtime path is too long");
        return 1;
    }
    if (mkdir_p(dir, 0700)) {
        perror(dir);
        return 1;
    }
    int lock = open(lockpath, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
        log_message("ERROR", "another instance is running");
        return 200;
    }
    ftruncate(lock, 0);
    dprintf(lock, "%ld\n", (long)getpid());
    int xfd = -1;
    xcb_connection_t *x = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(x)) {
        log_message("ERROR", "cannot connect to X11");
        return 1;
    }
    const xcb_setup_t *setup = xcb_get_setup(x);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen = it.data;
    xcb_window_t root = screen->root;
    snprintf(cfg.geometry, sizeof(cfg.geometry), "%ux%d+0+0", screen->width_in_pixels, cfg.height);
    xcb_atom_t active = atom(x, "_NET_ACTIVE_WINDOW"), utf8 = atom(x, "UTF8_STRING"),
               netname = atom(x, "_NET_WM_NAME");
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(x, root, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(x);
    xfd = xcb_get_file_descriptor(x);
    native_panel *panel = native_panel_create(x, screen, &cfg, error, sizeof(error));
    if (!panel) {
        log_message("ERROR", "%s", error);
        xcb_disconnect(x);
        return 1;
    }
    if (smoke_test) {
        panel_state smoke = {0};
        snprintf(smoke.workspace,
                 sizeof(smoke.workspace),
                 "%%{F%s}%%{B%s}%%{A3:notify|Native panel|space preserved:}"
                 "%%{A4:volume|up:}%%{A1:workspace|I:} native %%{A}%%{A}%%{A}%%{B-}%%{F-}",
                 cfg.color_focus,
                 cfg.color_bg);
        snprintf(smoke.title,
                 sizeof(smoke.title),
                 "%%{B%s}%%{F%s} Native X11 panel %%{F-}%%{B-}",
                 cfg.color_bg,
                 cfg.color_free);
        snprintf(smoke.clock,
                 sizeof(smoke.clock),
                 "%%{B%s}%%{F%s} smoke test %%{F-}%%{B-}",
                 cfg.color_bg,
                 cfg.color_clock);
        if (native_panel_draw(panel, &smoke)) {
            log_message("ERROR", "native smoke-test rendering failed");
            native_panel_destroy(panel);
            xcb_disconnect(x);
            close(lock);
            return 1;
        }
        if (smoke_test_native_tray(panel, x, screen)) {
            native_panel_destroy(panel);
            xcb_disconnect(x);
            close(lock);
            return 1;
        }
        const char *expected_actions[] = {
            "workspace|I", "notify|Native panel|space preserved", "volume|up"};
        const uint8_t buttons[] = {1, 3, 4};
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
            xcb_button_press_event_t click = {0};
            click.response_type = XCB_BUTTON_PRESS;
            click.event = native_panel_window(panel);
            click.event_x = 4;
            click.detail = buttons[i];
            char smoke_action[64] = "";
            bool redraw = false;
            if (!native_panel_handle_event(panel,
                                           (const xcb_generic_event_t *)&click,
                                           smoke_action,
                                           sizeof(smoke_action),
                                           &redraw) ||
                strcmp(smoke_action, expected_actions[i]) != 0) {
                log_message("ERROR", "native smoke-test action routing failed");
                native_panel_destroy(panel);
                xcb_disconnect(x);
                close(lock);
                return 1;
            }
        }
        usleep(100000);
        native_panel_destroy(panel);
        xcb_disconnect(x);
        close(lock);
        return 0;
    }
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGCHLD);
    sigprocmask(SIG_BLOCK, &signals, NULL);
    int sfd = signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    struct itimerspec tick = {{1, 0}, {0, 1}};
    timerfd_settime(tfd, 0, &tick, NULL);
    char *bsp_argv[] = {"bspc", "subscribe", "report", NULL};
    child bsp = {.read_fd = -1, .write_fd = -1};
    if (child_pipe(bsp_argv, false, &bsp)) {
        native_panel_destroy(panel);
        xcb_disconnect(x);
        return 1;
    }
    child network_events = {.read_fd = -1, .write_fd = -1};
    if (command_exists("nmcli")) {
        char *nm[] = {"nmcli", "monitor", NULL};
        if (child_pipe(nm, false, &network_events))
            log_message("ERROR", "cannot start nmcli monitor");
    }
    child title_root = {.read_fd = -1, .write_fd = -1};
    child title_window = {.read_fd = -1, .write_fd = -1};
#ifndef HAVE_XCB
    if (start_active_window_watcher(&title_root))
        log_message("ERROR", "cannot start active-window watcher");
    if (start_window_title_watcher(&title_window))
        log_message("ERROR", "cannot start window-title watcher");
#endif
    panel_state state = {0};
    pid_t weather_pid = start_weather_refresh(&cfg);
    module_static(&cfg, &state);
    module_clock(&cfg, &state);
    module_cpu(&cfg, &state);
    module_battery(&cfg, &state);
    module_screencast(&cfg, &state, runtime);
    module_volume(&cfg, &state);
    module_network(&cfg, &state);
    module_brightness(&cfg, &state);
    module_weather(&cfg, &state);
    update_title_xcb(x, root, active, utf8, netname, cfg.title_max, &state, &cfg);
    char report[PANEL_TEXT_MAX] = "", action[1024] = "";
    size_t report_used = 0;
    unsigned ticks = 0;
    bool running = true, dirty = true, vd = false, wd = false;
    while (running) {
        struct pollfd fds[] = {{tfd, POLLIN, 0},
                               {sfd, POLLIN, 0},
                               {bsp.read_fd, POLLIN, 0},
                               {-1, 0, 0},
                               {xfd, POLLIN, 0},
                               {network_events.read_fd, POLLIN, 0},
                               {title_root.read_fd, POLLIN, 0},
                               {title_window.read_fd, POLLIN, 0}};
        if (poll(fds, 8, -1) < 0) {
            if (errno == EINTR)
                continue;
            log_message("ERROR", "poll failed: %s", strerror(errno));
            break;
        }
        if (fds[0].revents & POLLIN) {
            uint64_t n;
            read(tfd, &n, sizeof(n));
            ticks += (unsigned)n;
            module_clock(&cfg, &state);
            update_title_xcb(x, root, active, utf8, netname, cfg.title_max, &state, &cfg);
            module_screencast(&cfg, &state, runtime);
            if (ticks % 5 == 0)
                module_cpu(&cfg, &state);
            if (ticks % 10 == 0)
                module_battery(&cfg, &state);
            if (ticks % cfg.network_interval == 0)
                module_network(&cfg, &state);
            if (ticks % cfg.weather_interval == 0 && weather_pid <= 0)
                weather_pid = start_weather_refresh(&cfg);
            if (network_events.pid <= 0 && command_exists("nmcli")) {
                char *nm[] = {"nmcli", "monitor", NULL};
                if (child_pipe(nm, false, &network_events))
                    log_message("ERROR", "cannot restart nmcli monitor");
            }
#ifndef HAVE_XCB
            if (title_root.pid <= 0) {
                if (start_active_window_watcher(&title_root))
                    log_message("ERROR", "cannot restart active-window watcher");
            }
            if (title_window.pid <= 0 && start_window_title_watcher(&title_window))
                log_message("ERROR", "cannot restart window-title watcher");
#endif
            dirty = true;
        }
        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo si;
            while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
                if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM)
                    running = false;
                else if (si.ssi_signo == SIGCHLD) {
                    pid_t reaped;
                    int status;
                    while ((reaped = waitpid(-1, &status, WNOHANG)) > 0) {
                        if (reaped == weather_pid) {
                            weather_pid = 0;
                            module_weather(&cfg, &state);
                            dirty = true;
                        } else if (reaped == bsp.pid) {
                            log_message("ERROR",
                                        "bspc report subscription exited: status=%d",
                                        WIFEXITED(status) ? WEXITSTATUS(status) : -1);
                            bsp.pid = 0;
                        } else if (reaped == network_events.pid) {
                            network_events.pid = 0;
                            if (network_events.read_fd >= 0)
                                close(network_events.read_fd);
                            network_events.read_fd = -1;
                        } else if (reaped == title_root.pid) {
                            title_root.pid = 0;
                            if (title_root.read_fd >= 0)
                                close(title_root.read_fd);
                            title_root.read_fd = -1;
                        } else if (reaped == title_window.pid) {
                            title_window.pid = 0;
                            if (title_window.read_fd >= 0)
                                close(title_window.read_fd);
                            title_window.read_fd = -1;
                        }
                    }
                }
            }
        }
        if (fds[2].revents & POLLIN) {
            ssize_t n = read(bsp.read_fd, report + report_used, sizeof(report) - report_used - 1);
            if (n > 0) {
                report_used += (size_t)n;
                report[report_used] = '\0';
                char *line = report;
                char *end = report + report_used;
                char *newline;
                while ((newline = memchr(line, '\n', (size_t)(end - line)))) {
                    *newline = '\0';
                    if (*line) {
                        module_workspace(&cfg, &state, line);
                        update_title_xcb(
                            x, root, active, utf8, netname, cfg.title_max, &state, &cfg);
                        dirty = true;
                    }
                    line = newline + 1;
                }
                report_used = (size_t)(end - line);
                memmove(report, line, report_used);
                report[report_used] = '\0';
            }
            if (report_used == sizeof(report) - 1) {
                log_message("ERROR", "discarding oversized bspwm report");
                report_used = 0;
                report[0] = '\0';
            }
        }
        if (fds[4].revents & POLLIN) {
            xcb_generic_event_t *ev;
            while ((ev = xcb_poll_for_event(x))) {
                bool redraw = false;
                if (native_panel_handle_event(panel, ev, action, sizeof(action), &redraw)) {
                    do_action(&cfg, action, &vd, &wd);
                    dirty = true;
                }
                if ((ev->response_type & 0x7fU) == XCB_PROPERTY_NOTIFY) {
                    update_title_xcb(x, root, active, utf8, netname, cfg.title_max, &state, &cfg);
                    dirty = true;
                }
                if (redraw)
                    dirty = true;
                free(ev);
            }
            if (vd) {
                module_volume(&cfg, &state);
                vd = false;
                dirty = true;
            }
            if (wd) {
                module_brightness(&cfg, &state);
                wd = false;
                dirty = true;
            }
        }
        if (fds[5].revents & POLLIN) {
            char discard[2048];
            while (read(network_events.read_fd, discard, sizeof(discard)) > 0) {
            }
            module_network(&cfg, &state);
            dirty = true;
        }
#ifndef HAVE_XCB
        if (fds[6].revents & POLLIN) {
            char event[2048];
            size_t used = 0;
            ssize_t count;
            while (used < sizeof(event) - 1 &&
                   (count = read(title_root.read_fd, event + used, sizeof(event) - used - 1)) > 0)
                used += (size_t)count;
            event[used] = '\0';
            retire_child(&title_window);
            char id[32];
            if (active_id_from_event(event, id, sizeof(id)) ||
                start_window_title_watcher_for_id(&title_window, id))
                log_message("ERROR", "cannot follow the active window title");
        }
        if (fds[7].revents & POLLIN) {
            char event[2048];
            size_t used = 0;
            ssize_t count;
            while (used < sizeof(event) - 1 &&
                   (count = read(title_window.read_fd, event + used, sizeof(event) - used - 1)) > 0)
                used += (size_t)count;
            event[used] = '\0';
            title_from_event(event, cfg.title_max, &state, &cfg);
            dirty = true;
        }
#endif
        if (dirty) {
            if (native_panel_draw(panel, &state)) {
                log_message("ERROR", "native panel rendering failed");
                running = false;
            }
            dirty = false;
        }
        if (bsp.pid <= 0 || kill(bsp.pid, 0)) {
            log_message("ERROR", "bspc report subscription is unavailable");
            running = false;
        }
        if (xcb_connection_has_error(x)) {
            log_message("ERROR", "X11 connection failed");
            running = false;
        }
    }
    close(tfd);
    close(sfd);
    if (weather_pid > 0) {
        kill(weather_pid, SIGTERM);
        waitpid(weather_pid, NULL, 0);
    }
    stop_child(&network_events);
    stop_child(&title_window);
    stop_child(&title_root);
    stop_child(&bsp);
    native_panel_destroy(panel);
    xcb_disconnect(x);
    close(lock);
    return 0;
#endif
}
