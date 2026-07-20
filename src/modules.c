#include "panel.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void
block(char *out, size_t n, const char *bg, const char *fg, const char *text) {
  snprintf(out, n, "%%{B%s}%%{F%s}%%{+u} %s %%{-u}%%{F-}%%{B-}", bg, fg, text);
}
static void
action(char *out, size_t n, int button, const char *command, const char *body) {
  snprintf(out, n, "%%{A%d:%s:}%s%%{A}", button, command, body);
}

void moduleClock(const PanelConfig *c, PanelState *s) {
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  char d[64], text[160];
  strftime(d, sizeof(d), "%a %b %d", &tm);
  char t[32];
  strftime(t, sizeof(t), "%T", &tm);
  snprintf(text, sizeof(text), " %s  %s", d, t);
  block(s->clock, sizeof(s->clock), c->colorBg, c->colorClock, text);
}

void moduleCpu(const PanelConfig *c, PanelState *s) {
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
  if (s->cpuInitialized && total > s->cpuTotal) {
    uint64_t dt = total - s->cpuTotal, di = idle - s->cpuIdle;
    if (di <= dt)
      use = 100.0 * (double)(dt - di) / (double)dt;
  }
  s->cpuTotal = total;
  s->cpuIdle = idle;
  s->cpuInitialized = true;
  char text[64], body[256], usage[16];
  snprintf(usage, sizeof(usage), "%.1f", use);
  int padding = 5 - (int)strlen(usage);
  if (padding < 0)
    padding = 0;
  snprintf(text, sizeof(text), " %s%%%*s", usage, padding, "");
  block(body, sizeof(body), c->colorBg, c->colorSystem, text);
  action(s->cpu, sizeof(s->cpu), 1, "terminal|btop", body);
}

void moduleBattery(const PanelConfig *c, PanelState *s) {
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
      if (!readTextFile(p, v, sizeof(v))) {
        char *end;
        long x = strtol(v, &end, 10);
        if (end != v && x >= 0 && x <= 100) {
          sum += (int)x;
          count++;
        }
      }
      snprintf(p, sizeof(p), "/sys/class/power_supply/%s/status", e->d_name);
      if (!readTextFile(p, v, sizeof(v))) {
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
    snprintf(text,
             sizeof(text),
             "%s %3d%% %s",
             icon,
             p,
             charging ? ""
             : full   ? ""
                      : " ");
  }
  const char *fg = charging                       ? c->colorFocus
                   : (count && sum / count <= 10) ? c->colorCritical
                   : (count && sum / count <= 20) ? c->colorWarning
                                                  : c->colorBattery;
  block(s->battery, sizeof(s->battery), c->colorBg, fg, text);
}

void moduleScreencast(const PanelConfig *c,
                      PanelState *s,
                      const char *runtime) {
  char p[PANEL_PATH_MAX];
  snprintf(p, sizeof(p), "%s/screencast.pid", runtime);
  block(s->screencast,
        sizeof(s->screencast),
        c->colorBg,
        access(p, F_OK) == 0 ? c->colorCritical : c->colorFree,
        "壘");
}

void moduleVolume(const PanelConfig *c, PanelState *s) {
  char out[2048];
  char *argv[] = {"amixer", "get", "Master", NULL};
  if (runCapture(argv, out, sizeof(out), 1000))
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
  char levelText[8];
  snprintf(levelText, sizeof(levelText), "%d", level);
  int padding = 3 - (int)strlen(levelText);
  if (padding < 0)
    padding = 0;
  snprintf(text,
           sizeof(text),
           "%s %s%%%*s",
           muted ? "" : "",
           levelText,
           padding,
           "");
  block(body,
        sizeof(body),
        c->colorBg,
        muted ? c->colorMuted : c->colorVolume,
        text);
  action(tmp, sizeof(tmp), 1, "terminal|pulsemixer", body);
  char middle[768];
  action(middle, sizeof(middle), 3, "volume|toggle", tmp);
  char down[1024];
  action(down, sizeof(down), 5, "volume|down", middle);
  action(s->volume, sizeof(s->volume), 4, "volume|up", down);
}

int parseNmcliWifi(const char *output,
                   char *ssid,
                   size_t ssidSize,
                   int *strength) {
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
        size_t numberLength = (size_t)(line + length - last);
        if (numberLength < sizeof(number)) {
          memcpy(number, last, numberLength);
          number[numberLength] = '\0';
          char *numberEnd;
          long value = strtol(number, &numberEnd, 10);
          if (*number && !*numberEnd && value >= 0 && value <= 100) {
            size_t nameLength = (size_t)(last - payload - 1);
            if (nameLength >= ssidSize)
              nameLength = ssidSize - 1;
            memcpy(ssid, payload, nameLength);
            ssid[nameLength] = '\0';
            if (!ssid[0])
              snprintf(ssid, ssidSize, "-");
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

int wifiQualityPercent(double quality) {
  if (quality != quality || quality < 0.0)
    return -1;
  if (quality >= 70.0)
    return 100;
  return (int)((quality * 100.0 + 35.0) / 70.0);
}

int parseDefaultRouteInterface(const char *routes,
                               char *interface,
                               size_t interfaceSize) {
  if (!routes || !interface || interfaceSize == 0)
    return -1;
  const char *line = strchr(routes, '\n');
  line = line ? line + 1 : routes;
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t length = end ? (size_t)(end - line) : strlen(line);
    char copy[512], name[128];
    if (length >= sizeof(copy))
      length = sizeof(copy) - 1;
    memcpy(copy, line, length);
    copy[length] = '\0';
    unsigned long destination, gateway, flags;
    int fields =
        sscanf(copy, "%127s %lx %lx %lx", name, &destination, &gateway, &flags);
    (void)gateway;
    if (fields == 4 && destination == 0 && (flags & 1UL)) {
      snprintf(interface, interfaceSize, "%s", name);
      return 0;
    }
    line = end ? end + 1 : line + length;
  }
  interface[0] = '\0';
  return -1;
}

static void defaultRouteInterface(char *interface, size_t interfaceSize) {
  char routes[16384];
  interface[0] = '\0';
  if (!readTextFile("/proc/net/route", routes, sizeof(routes)))
    parseDefaultRouteInterface(routes, interface, interfaceSize);
}

static int wirelessStrength(const char *interface) {
  FILE *file = fopen("/proc/net/wireless", "r");
  if (!file)
    return -1;
  char line[512], name[128];
  double quality;
  int strength = -1;
  while (fgets(line, sizeof(line), file)) {
    if (sscanf(line, " %127[^:]: %*s %lf", name, &quality) == 2 &&
        !strcmp(name, interface)) {
      strength = wifiQualityPercent(quality);
      break;
    }
  }
  fclose(file);
  return strength;
}

void moduleNetwork(const PanelConfig *c, PanelState *s) {
  DIR *d = opendir("/sys/class/net");
  bool eth = false, wifi = false;
  char ssid[128] = "-", wifiInterface[256] = "", preferredInterface[256];
  int strength = -1;
  defaultRouteInterface(preferredInterface, sizeof(preferredInterface));
  if (d) {
    struct dirent *e;
    while ((e = readdir(d))) {
      if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo"))
        continue;
      char p[PANEL_PATH_MAX], v[32];
      snprintf(p, sizeof(p), "/sys/class/net/%s/operstate", e->d_name);
      if (readTextFile(p, v, sizeof(v)) || strcmp(v, "up") != 0)
        continue;
      snprintf(p, sizeof(p), "/sys/class/net/%s/wireless", e->d_name);
      char phy[PANEL_PATH_MAX];
      snprintf(phy, sizeof(phy), "/sys/class/net/%s/phy80211", e->d_name);
      if (!access(p, F_OK) || !access(phy, F_OK)) {
        wifi = true;
        if (!wifiInterface[0] || !strcmp(e->d_name, preferredInterface))
          snprintf(wifiInterface, sizeof(wifiInterface), "%s", e->d_name);
      } else {
        snprintf(p, sizeof(p), "/sys/class/net/%s/device", e->d_name);
        if (!access(p, F_OK))
          eth = true;
      }
    }
    closedir(d);
  }
  int kernelStrength = wifi ? wirelessStrength(wifiInterface) : -1;
  int nmcliStrength = -1;
  if (wifi && commandExists("nmcli")) {
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
                  wifiInterface,
                  NULL};
    if (!runCapture(av, out, sizeof(out), 1500))
      parseNmcliWifi(out, ssid, sizeof(ssid), &nmcliStrength);
  }
  strength = kernelStrength >= 0 ? kernelStrength : nmcliStrength;
  char text[256], body[512], safe[128], wifiText[64] = "";
  shellQuoteAction(ssid, safe, sizeof(safe));
  if (wifi && strength >= 0) {
    char strengthText[16];
    snprintf(strengthText, sizeof(strengthText), "%d", strength);
    int padding = 3 - (int)strlen(strengthText);
    if (padding < 0)
      padding = 0;
    snprintf(
        wifiText, sizeof(wifiText), "說 %s%%%*s", strengthText, padding, "");
  } else if (wifi)
    snprintf(wifiText, sizeof(wifiText), "說");
  if (eth && wifiText[0])
    snprintf(text, sizeof(text), " %s", wifiText);
  else
    snprintf(text, sizeof(text), "%s", eth ? "" : wifiText);
  block(body, sizeof(body), c->colorBg, c->colorNetwork, text);
  char tmp[768];
  char cmd[180];
  snprintf(cmd, sizeof(cmd), "notify|Network|%s", safe);
  action(tmp, sizeof(tmp), 3, cmd, body);
  action(s->network, sizeof(s->network), 1, "terminal|nmtui", tmp);
}

void moduleBrightnessValue(const PanelConfig *c, PanelState *s, int pct) {
  char text[64], body[256], tmp[512];
  snprintf(text, sizeof(text), " %3d%%", pct);
  block(body, sizeof(body), c->colorBg, c->colorBrightness, text);
  action(tmp, sizeof(tmp), 5, "brightness|down", body);
  action(s->brightness, sizeof(s->brightness), 4, "brightness|up", tmp);
}

void moduleBrightness(const PanelConfig *c, PanelState *s) {
  char query[16384], output[64] = "";
  char *qv[] = {"xrandr", "--query", NULL};
  if (!runCapture(qv, query, sizeof(query), 1200)) {
    char *save = NULL;
    for (char *line = strtok_r(query, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
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
  if (*output && !runCapture(av, out, sizeof(out), 1500)) {
    char *section = out;
    while ((section = strstr(section, output))) {
      if ((section == out || section[-1] == '\n') &&
          section[strlen(output)] == ' ')
        break;
      section += strlen(output);
    }
    char *p = section ? strstr(section, "Brightness:") : NULL;
    if (p)
      pct = (int)(strtod(p + 11, NULL) * 100.0 + 0.5);
  }
  if (*output) {
    snprintf(s->brightnessOutput, sizeof(s->brightnessOutput), "%s", output);
    s->brightnessPercent = pct;
    s->brightnessInitialized = true;
  }
  moduleBrightnessValue(c, s, pct);
}

static int jsonInteger(char *p) {
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
void moduleWeather(const PanelConfig *c, PanelState *s) {
  char data[32768] = "";
  if (*c->weatherCache)
    readTextFile(c->weatherCache, data, sizeof(data));
  int rain = 0, min = 0, max = 0;
  char *p = strstr(data, "\"chanceofrain\"");
  for (int i = 0; p && i < 8; i++, p = strstr(p + 1, "\"chanceofrain\"")) {
    int v = jsonInteger(p);
    if (v > rain)
      rain = v;
  }
  p = strstr(data, "\"mintempC\"");
  min = jsonInteger(p);
  p = strstr(data, "\"maxtempC\"");
  max = jsonInteger(p);
  char text[96], body[256], tmp[512];
  snprintf(text, sizeof(text), "爫%3d%% %3d° %3d°", rain, min, max);
  block(body, sizeof(body), c->colorBg, c->colorWeather, text);
  action(tmp, sizeof(tmp), 3, "weather|notify", body);
  action(s->weather, sizeof(s->weather), 1, "weather|open", tmp);
}

void moduleWorkspace(const PanelConfig *c, PanelState *s, const char *report) {
  static const char *icons[] = {
      "", "", "", "", "", "", "", "", ""};
  s->workspace[0] = '\0';
  s->focusedWorkspaceKnown = false;
  s->focusedWorkspaceOccupied = false;
  if (!report)
    return;
  char copybuf[PANEL_TEXT_MAX];
  snprintf(
      copybuf, sizeof(copybuf), "%s", report[0] == 'W' ? report + 1 : report);
  bool focused = false;
  unsigned idx = 0;
  char layout = '?';
  char *save = NULL;
  for (char *it = strtok_r(copybuf, ":", &save); it;
       it = strtok_r(NULL, ":", &save)) {
    char type = *it, name[128];
    snprintf(name, sizeof(name), "%s", it + 1);
    if (type == 'M' || type == 'm') {
      focused = type == 'M';
      idx = 0;
    } else if (strchr("OoFfUu", type) && focused) {
      if (isupper((unsigned char)type)) {
        s->focusedWorkspaceKnown = true;
        s->focusedWorkspaceOccupied = type != 'F';
      }
      const char *fg = c->colorFree, *bg = c->colorFreeBg;
      if (type == 'O' || type == 'U') {
        fg = c->colorFocusedOccupied;
        bg = c->colorFocusedOccupiedBg;
      } else if (type == 'o') {
        fg = c->colorOccupied;
        bg = c->colorOccupiedBg;
      } else if (type == 'F') {
        fg = c->colorFocusedFree;
        bg = c->colorFocusedFreeBg;
      } else if (type == 'u') {
        fg = c->colorUrgent;
        bg = c->colorUrgentBg;
      }
      char part[512];
      const char *tag = idx < 9 ? icons[idx] : name;
      snprintf(part,
               sizeof(part),
               "%%{F%s}%%{B%s}%%{U%s}%%{+u}%%{A1:workspace|%s:} %s "
               "%%{A}%%{B-}%%{F-}%%{-u}",
               fg,
               bg,
               c->colorNetwork,
               name,
               tag);
      strncat(
          s->workspace, part, sizeof(s->workspace) - strlen(s->workspace) - 1);
      idx++;
    } else if (type == 'L' && focused)
      layout = *name;
  }
  char tail[128];
  snprintf(tail,
           sizeof(tail),
           "%%{F%s}%%{B%s} %s %%{B-}%%{F-}",
           c->colorFree,
           c->colorBg,
           layout == 'T'   ? "[TILED]"
           : layout == 'M' ? "[MONOCLE]"
                           : "[UNKNOWN]");
  strncat(s->workspace, tail, sizeof(s->workspace) - strlen(s->workspace) - 1);
}

void moduleStatic(const PanelConfig *c, PanelState *s) {
  char body[128];
  block(body, sizeof(body), c->colorBg, c->colorFg, "");
  action(s->launcher, sizeof(s->launcher), 1, "launcher", body);
  block(body, sizeof(body), c->colorBg, c->colorFg, "");
  action(s->power, sizeof(s->power), 1, "power", body);
}

void renderPanel(const PanelState *s, char *out, size_t n) {
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
