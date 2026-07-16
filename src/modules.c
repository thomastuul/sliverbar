#include "panel.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void block(char *out, size_t n, const char *bg, const char *fg, const char *text) {
    snprintf(out, n, "%%{B%s}%%{F%s}%%{+u} %s %%{-u}%%{F-}%%{B-}", bg, fg, text);
}
static void action(char *out, size_t n, int button, const char *command, const char *body) {
    snprintf(out, n, "%%{A%d:%s:}%s%%{A}", button, command, body);
}

void module_clock(const panel_config *c, panel_state *s) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char d[64], text[160];
    strftime(d, sizeof(d), "%a %b %d", &tm);
    char t[32];
    strftime(t, sizeof(t), "%T", &tm);
    snprintf(text, sizeof(text), " %s  %s", d, t);
    block(s->clock, sizeof(s->clock), c->color_bg, c->color_clock, text);
}

void module_cpu(const panel_config *c, panel_state *s) {
    FILE *f = fopen("/proc/stat", "r");
    unsigned long long u, n, sy, id, io, ir, si, st;
    if (!f || fscanf(f,
                     "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &u,
                     &n,
                     &sy,
                     &id,
                     &io,
                     &ir,
                     &si,
                     &st) != 8) {
        if (f)
            fclose(f);
        return;
    }
    fclose(f);
    uint64_t total = u + n + sy + id + io + ir + si + st, idle = id + io;
    double use = 0;
    if (s->cpu_initialized && total > s->cpu_total) {
        uint64_t dt = total - s->cpu_total, di = idle - s->cpu_idle;
        if (di <= dt)
            use = 100.0 * (double)(dt - di) / (double)dt;
    }
    s->cpu_total = total;
    s->cpu_idle = idle;
    s->cpu_initialized = true;
    char text[64], body[256];
    snprintf(text, sizeof(text), " %5.1f%%", use);
    block(body, sizeof(body), c->color_bg, c->color_system, text);
    action(s->cpu, sizeof(s->cpu), 1, "terminal|btop", body);
}

void module_battery(const panel_config *c, panel_state *s) {
    DIR *d = opendir("/sys/class/power_supply");
    int sum = 0, count = 0;
    bool charging = false, full = true;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strncmp(e->d_name, "BAT", 3) != 0)
                continue;
            char p[PANEL_PATH_MAX], v[64];
            snprintf(p, sizeof(p), "/sys/class/power_supply/%s/capacity", e->d_name);
            if (!read_text_file(p, v, sizeof(v))) {
                char *end;
                long x = strtol(v, &end, 10);
                if (end != v && x >= 0 && x <= 100) {
                    sum += (int)x;
                    count++;
                }
            }
            snprintf(p, sizeof(p), "/sys/class/power_supply/%s/status", e->d_name);
            if (!read_text_file(p, v, sizeof(v))) {
                if (!strcasecmp(v, "Charging")) {
                    charging = true;
                    full = false;
                } else if (strcasecmp(v, "Full") != 0)
                    full = false;
            }
        }
        closedir(d);
    }
    char text[96];
    if (!count)
        snprintf(text, sizeof(text), " AC");
    else {
        int p = sum / count;
        const char *icon = p >= 95   ? ""
                           : p >= 75 ? ""
                           : p >= 50 ? ""
                           : p >= 25 ? ""
                                     : "";
        snprintf(text, sizeof(text), "%s %3d%% %s", icon, p, charging ? "" : full ? "" : " ");
    }
    const char *fg = charging                       ? c->color_focus
                     : (count && sum / count <= 10) ? c->color_critical
                     : (count && sum / count <= 20) ? c->color_warning
                                                    : c->color_battery;
    block(s->battery, sizeof(s->battery), c->color_bg, fg, text);
}

void module_screencast(const panel_config *c, panel_state *s, const char *runtime) {
    char p[PANEL_PATH_MAX];
    snprintf(p, sizeof(p), "%s/screencast.pid", runtime);
    block(s->screencast,
          sizeof(s->screencast),
          c->color_bg,
          access(p, F_OK) == 0 ? c->color_critical : c->color_free,
          "壘");
}

void module_volume(const panel_config *c, panel_state *s) {
    char out[2048];
    char *argv[] = {"amixer", "get", "Master", NULL};
    if (run_capture(argv, out, sizeof(out), 1000))
        return;
    char *p = strchr(out, '%');
    int level = 0;
    bool muted = strstr(out, "[off]") != NULL;
    if (p) {
        char *q = p;
        while (q > out && q[-1] >= '0' && q[-1] <= '9')
            q--;
        level = atoi(q);
    }
    char text[64], body[256], tmp[512];
    snprintf(text, sizeof(text), "%s %3d%%", muted ? "" : "", level);
    block(body, sizeof(body), c->color_bg, muted ? c->color_muted : c->color_volume, text);
    action(tmp, sizeof(tmp), 3, "volume|toggle", body);
    char a[768];
    action(a, sizeof(a), 5, "volume|down", tmp);
    action(s->volume, sizeof(s->volume), 4, "volume|up", a);
}

int parse_nmcli_wifi(const char *output, char *ssid, size_t ssid_size, int *strength) {
    const char *line = output;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        const char *payload = NULL;
        if (length >= 2 && line[0] == '*' && line[1] == ':')
            payload = line + 2;
        else if (length >= 4 && !strncmp(line, "yes:", 4))
            payload = line + 4;
        if (payload) {
            const char *last = line + length;
            while (last > payload && last[-1] != ':')
                last--;
            if (last > payload) {
                char number[16];
                size_t number_length = (size_t)(line + length - last);
                if (number_length < sizeof(number)) {
                    memcpy(number, last, number_length);
                    number[number_length] = '\0';
                    char *number_end;
                    long value = strtol(number, &number_end, 10);
                    if (*number && !*number_end && value >= 0 && value <= 100) {
                        size_t name_length = (size_t)(last - payload - 1);
                        if (name_length >= ssid_size)
                            name_length = ssid_size - 1;
                        memcpy(ssid, payload, name_length);
                        ssid[name_length] = '\0';
                        if (!ssid[0])
                            snprintf(ssid, ssid_size, "-");
                        *strength = (int)value;
                        return 0;
                    }
                }
            }
        }
        line = end ? end + 1 : NULL;
    }
    return -1;
}

static int wireless_strength(const char *interface) {
    FILE *file = fopen("/proc/net/wireless", "r");
    if (!file)
        return -1;
    char line[512], name[128];
    double quality;
    int strength = -1;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, " %127[^:]: %*s %lf", name, &quality) == 2 && !strcmp(name, interface)) {
            strength = (int)(quality * 100.0 / 70.0);
            if (strength > 100)
                strength = 100;
            if (strength < 0)
                strength = 0;
            break;
        }
    }
    fclose(file);
    return strength;
}

void module_network(const panel_config *c, panel_state *s) {
    DIR *d = opendir("/sys/class/net");
    bool eth = false, wifi = false;
    char ssid[128] = "-", wifi_interface[256] = "";
    int strength = -1;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo"))
                continue;
            char p[PANEL_PATH_MAX], v[32];
            snprintf(p, sizeof(p), "/sys/class/net/%s/operstate", e->d_name);
            if (read_text_file(p, v, sizeof(v)) || strcmp(v, "up") != 0)
                continue;
            snprintf(p, sizeof(p), "/sys/class/net/%s/wireless", e->d_name);
            char phy[PANEL_PATH_MAX];
            snprintf(phy, sizeof(phy), "/sys/class/net/%s/phy80211", e->d_name);
            if (!access(p, F_OK) || !access(phy, F_OK)) {
                wifi = true;
                if (!wifi_interface[0])
                    snprintf(wifi_interface, sizeof(wifi_interface), "%s", e->d_name);
            } else
                eth = true;
        }
        closedir(d);
    }
    if (wifi && command_exists("nmcli")) {
        char out[2048];
        char *av[] = {"nmcli",
                      "--terse",
                      "--escape",
                      "no",
                      "--fields",
                      "IN-USE,SSID,SIGNAL",
                      "device",
                      "wifi",
                      "list",
                      "--rescan",
                      "no",
                      "ifname",
                      wifi_interface,
                      NULL};
        if (run_capture(av, out, sizeof(out), 1500) ||
            parse_nmcli_wifi(out, ssid, sizeof(ssid), &strength))
            strength = wireless_strength(wifi_interface);
    } else if (wifi) {
        strength = wireless_strength(wifi_interface);
    }
    char text[256], body[512], safe[128];
    shell_quote_action(ssid, safe, sizeof(safe));
    if (wifi && strength >= 0)
        snprintf(text, sizeof(text), "%s %s %3d%%", eth ? "" : "", "說", strength);
    else
        snprintf(text, sizeof(text), "%s %s", eth ? "" : "", wifi ? "說" : "");
    block(body, sizeof(body), c->color_bg, c->color_network, text);
    char tmp[768];
    char cmd[180];
    snprintf(cmd, sizeof(cmd), "notify|Network|%s", safe);
    action(tmp, sizeof(tmp), 3, cmd, body);
    action(s->network, sizeof(s->network), 1, "terminal|nmtui", tmp);
}

void module_brightness(const panel_config *c, panel_state *s) {
    char query[16384], output[64] = "";
    char *qv[] = {"xrandr", "--query", NULL};
    if (!run_capture(qv, query, sizeof(query), 1200)) {
        char *save = NULL;
        for (char *line = strtok_r(query, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char state[32], geometry[64];
            if (sscanf(line, "%63s %31s %63s", output, state, geometry) == 3 &&
                !strcmp(state, "connected") && strchr(geometry, '+'))
                break;
            output[0] = '\0';
        }
    }
    char out[32768];
    char *av[] = {"xrandr", "--verbose", "--current", NULL};
    int pct = 100;
    if (*output && !run_capture(av, out, sizeof(out), 1500)) {
        char *section = out;
        while ((section = strstr(section, output))) {
            if ((section == out || section[-1] == '\n') && section[strlen(output)] == ' ')
                break;
            section += strlen(output);
        }
        char *p = section ? strstr(section, "Brightness:") : NULL;
        if (p)
            pct = (int)(strtod(p + 11, NULL) * 100.0 + 0.5);
    }
    char text[64], body[256], tmp[512];
    snprintf(text, sizeof(text), " %3d%%", pct);
    block(body, sizeof(body), c->color_bg, c->color_brightness, text);
    action(tmp, sizeof(tmp), 5, "brightness|down", body);
    action(s->brightness, sizeof(s->brightness), 4, "brightness|up", tmp);
}

int parse_xdotool_width(const char *output) {
    int largest = 0;
    const char *line = output;
    while (line && *line) {
        if (!strncmp(line, "WIDTH=", 6)) {
            char *end;
            long width = strtol(line + 6, &end, 10);
            if (end != line + 6 && width > largest && width <= INT_MAX)
                largest = (int)width;
        }
        line = strchr(line, '\n');
        if (line)
            line++;
    }
    return largest;
}

void module_tray(const panel_config *c, panel_state *s) {
    char out[2048];
    int w = 0;
    if (command_exists("xdotool")) {
        char *raise[] = {"xdotool", "search", "--class", "trayer", "windowraise", NULL};
        run_capture(raise, out, sizeof(out), 800);
    }
    char *hints[] = {"xprop", "-name", "panel", "WM_NORMAL_HINTS", NULL};
    if (!run_capture(hints, out, sizeof(out), 800)) {
        char *minimum = strstr(out, "minimum size:");
        if (minimum)
            w = atoi(minimum + 13);
    }
    if (w <= 0 && command_exists("xdotool")) {
        char *geometry[] = {
            "xdotool", "search", "--class", "trayer", "getwindowgeometry", "--shell", NULL};
        if (!run_capture(geometry, out, sizeof(out), 800))
            w = parse_xdotool_width(out);
    }
    if (w <= 0 && s->tray[0])
        return;
    if (w <= 0)
        w = 1;
    snprintf(s->tray,
             sizeof(s->tray),
             "%%{F%s}%%{B%s}%%{O%d}%%{B-}%%{F-}",
             c->color_fg,
             c->color_bg,
             w + 4);
}

static int json_integer(char *p) {
    if (!p)
        return 0;
    p = strchr(p, ':');
    if (!p)
        return 0;
    p++;
    while (*p && !(isdigit((unsigned char)*p) || *p == '-'))
        p++;
    return atoi(p);
}
void module_weather(const panel_config *c, panel_state *s) {
    char data[32768] = "";
    if (*c->weather_cache)
        read_text_file(c->weather_cache, data, sizeof(data));
    int rain = 0, min = 0, max = 0;
    char *p = strstr(data, "\"chanceofrain\"");
    for (int i = 0; p && i < 8; i++, p = strstr(p + 1, "\"chanceofrain\"")) {
        int v = json_integer(p);
        if (v > rain)
            rain = v;
    }
    p = strstr(data, "\"mintempC\"");
    min = json_integer(p);
    p = strstr(data, "\"maxtempC\"");
    max = json_integer(p);
    char text[96], body[256], tmp[512];
    snprintf(text, sizeof(text), "爫%3d%% %3d° %3d°", rain, min, max);
    block(body, sizeof(body), c->color_bg, c->color_weather, text);
    action(tmp, sizeof(tmp), 3, "weather|notify", body);
    action(s->weather, sizeof(s->weather), 1, "weather|open", tmp);
}

void module_workspace(const panel_config *c, panel_state *s, const char *report) {
    static const char *icons[] = {"", "", "", "", "", "", "", "", ""};
    s->workspace[0] = '\0';
    if (!report)
        return;
    char copybuf[PANEL_TEXT_MAX];
    snprintf(copybuf, sizeof(copybuf), "%s", report[0] == 'W' ? report + 1 : report);
    bool focused = false;
    unsigned idx = 0;
    char layout = '?';
    char *save = NULL;
    for (char *it = strtok_r(copybuf, ":", &save); it; it = strtok_r(NULL, ":", &save)) {
        char type = *it, name[128];
        snprintf(name, sizeof(name), "%s", it + 1);
        if (type == 'M' || type == 'm') {
            focused = type == 'M';
            idx = 0;
        } else if (strchr("OoFfUu", type) && focused) {
            const char *fg = c->color_free, *bg = c->color_free_bg;
            if (type == 'O') {
                fg = c->color_focused_occupied;
                bg = c->color_focused_occupied_bg;
            } else if (type == 'o') {
                fg = c->color_occupied;
                bg = c->color_occupied_bg;
            } else if (type == 'F') {
                fg = c->color_focused_free;
                bg = c->color_focused_free_bg;
            } else if (type == 'U') {
                fg = c->color_focused_urgent;
                bg = c->color_focused_urgent_bg;
            } else if (type == 'u') {
                fg = c->color_urgent;
                bg = c->color_urgent_bg;
            }
            char part[512];
            const char *tag = idx < 9 ? icons[idx] : name;
            snprintf(part,
                     sizeof(part),
                     "%%{F%s}%%{B%s}%%{U%s}%%{+u}%%{A1:workspace|%s:} %s %%{A}%%{B-}%%{F-}%%{-u}",
                     fg,
                     bg,
                     c->color_network,
                     name,
                     tag);
            strncat(s->workspace, part, sizeof(s->workspace) - strlen(s->workspace) - 1);
            idx++;
        } else if (type == 'L' && focused)
            layout = *name;
    }
    char tail[128];
    snprintf(tail,
             sizeof(tail),
             "%%{F%s}%%{B%s} %s %%{B-}%%{F-}",
             c->color_free,
             c->color_bg,
             layout == 'T'   ? "[TILED]"
             : layout == 'M' ? "[MONOCLE]"
                             : "[UNKNOWN]");
    strncat(s->workspace, tail, sizeof(s->workspace) - strlen(s->workspace) - 1);
}

void module_static(const panel_config *c, panel_state *s) {
    char body[128];
    block(body, sizeof(body), c->color_bg, c->color_fg, "");
    action(s->launcher, sizeof(s->launcher), 1, "launcher", body);
    block(body, sizeof(body), c->color_bg, c->color_fg, "");
    action(s->power, sizeof(s->power), 1, "power", body);
}

void render_panel(const panel_state *s, char *out, size_t n) {
    snprintf(out,
             n,
             "%%{l}%s%s%%{c}%s%%{r}%s%s%s%s%s%s%s%s%s%s\n",
             s->launcher,
             s->workspace,
             s->title,
             s->screencast,
             s->weather,
             s->battery,
             s->network,
             s->brightness,
             s->volume,
             s->cpu,
             s->clock,
             s->tray,
             s->power);
}
