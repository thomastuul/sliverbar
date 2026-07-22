#include "panel.h"

#include "app_launcher.h"

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

const char *moduleClockGlyph(const PanelConfig *c, unsigned hour) {
  static const char *const CLOCK_GLYPHS[] = {"󱑋",
                                             "󱑌",
                                             "󱑍",
                                             "󱑎",
                                             "󱑏",
                                             "󱑐",
                                             "󱑑",
                                             "󱑒",
                                             "󱑓",
                                             "󱑔",
                                             "󱑕",
                                             "󱑖"};
  unsigned clockHour = hour % 12U;
  return c && c->iconFont[0] ? CLOCK_GLYPHS[clockHour ? clockHour - 1U : 11U]
                             : "◷";
}

void moduleClock(const PanelConfig *c, PanelState *s) {
  s->clock[0] = '\0';
  if (!moduleModeActive(c->moduleClock, true))
    return;
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  static const char *const DAYS_EN[] = {
      "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const DAYS_DE[] = {
      "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  static const char *const MONTHS_EN[] = {"Jan",
                                          "Feb",
                                          "Mar",
                                          "Apr",
                                          "May",
                                          "Jun",
                                          "Jul",
                                          "Aug",
                                          "Sep",
                                          "Oct",
                                          "Nov",
                                          "Dec"};
  static const char *const MONTHS_DE[] = {"Jan",
                                          "Feb",
                                          "Mär",
                                          "Apr",
                                          "Mai",
                                          "Jun",
                                          "Jul",
                                          "Aug",
                                          "Sep",
                                          "Okt",
                                          "Nov",
                                          "Dez"};
  bool german = panelLanguageIsGerman(c);
  char d[64], text[160];
  snprintf(d,
           sizeof(d),
           "%s %s %02d",
           german ? DAYS_DE[tm.tm_wday] : DAYS_EN[tm.tm_wday],
           german ? MONTHS_DE[tm.tm_mon] : MONTHS_EN[tm.tm_mon],
           tm.tm_mday);
  char t[32];
  strftime(t, sizeof(t), "%T", &tm);
  const char *clockGlyph = moduleClockGlyph(c, (unsigned)tm.tm_hour);
  snprintf(text, sizeof(text), " %s %s %s", d, clockGlyph, t);
  char body[256];
  block(body, sizeof(body), c->colorBg, c->colorClock, text);
  if (appRoleAvailable(c, APP_ROLE_CALENDAR))
    action(s->clock, sizeof(s->clock), 1, "role|calendar", body);
  else
    snprintf(s->clock, sizeof(s->clock), "%s", body);
}

void moduleCpu(const PanelConfig *c, PanelState *s) {
  s->cpu[0] = '\0';
  if (!moduleModeActive(c->moduleCpu, true))
    return;
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
  if (appRoleAvailable(c, APP_ROLE_SYSTEM_MONITOR))
    action(s->cpu, sizeof(s->cpu), 1, "role|system_monitor", body);
  else
    snprintf(s->cpu, sizeof(s->cpu), "%s", body);
}

void moduleBattery(const PanelConfig *c, PanelState *s) {
  s->battery[0] = '\0';
  if (c->moduleBattery == MODULE_DISABLED)
    return;
  DIR *d = opendir("/sys/class/power_supply");
  bool powerSupplyAvailable = d != NULL;
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
  if (!moduleModeActive(c->moduleBattery, powerSupplyAvailable))
    return;
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
  bool active = access(p, F_OK) == 0;
  s->screencast[0] = '\0';
  if (!moduleModeActive(c->moduleScreencast, active))
    return;
  block(s->screencast,
        sizeof(s->screencast),
        c->colorBg,
        active ? c->colorCritical : c->colorFree,
        "壘");
}

void moduleVolume(const PanelConfig *c, PanelState *s) {
  s->volume[0] = '\0';
  bool pactl = commandExists("pactl");
  bool amixer = commandExists("amixer");
  if (!moduleModeActive(c->moduleVolume, pactl || amixer) ||
      (!pactl && !amixer))
    return;
  char out[2048];
  bool muted = false;
  if (pactl) {
    char *volumeArgv[] = {"pactl", "get-sink-volume", "@DEFAULT_SINK@", NULL};
    if (runCapture(volumeArgv, out, sizeof(out), 1000))
      return;
    char muteOutput[128];
    char *muteArgv[] = {"pactl", "get-sink-mute", "@DEFAULT_SINK@", NULL};
    if (!runCapture(muteArgv, muteOutput, sizeof(muteOutput), 1000))
      muted = strstr(muteOutput, "yes") != NULL;
  } else {
    char *volumeArgv[] = {"amixer", "get", "Master", NULL};
    if (runCapture(volumeArgv, out, sizeof(out), 1000))
      return;
    muted = strstr(out, "[off]") != NULL;
  }
  char *p = strchr(out, '%');
  int level = 0;
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
  if (appRoleAvailable(c, APP_ROLE_VOLUME_SETTINGS))
    action(tmp, sizeof(tmp), 1, "role|volume_settings", body);
  else
    snprintf(tmp, sizeof(tmp), "%s", body);
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

int parseWirelessQuality(const char *contents,
                         const char *interface,
                         double *quality,
                         int *percent) {
  if (!contents || !interface || !quality || !percent)
    return -1;
  const char *line = contents;
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t length = end ? (size_t)(end - line) : strlen(line);
    char copy[512], name[128];
    if (length >= sizeof(copy))
      length = sizeof(copy) - 1;
    memcpy(copy, line, length);
    copy[length] = '\0';
    double value = 0.0;
    if (sscanf(copy, " %127[^:]: %*s %lf", name, &value) == 2 &&
        !strcmp(name, interface)) {
      int converted = wifiQualityPercent(value);
      if (converted < 0)
        return -1;
      *quality = value;
      *percent = converted;
      return 0;
    }
    line = end ? end + 1 : line + length;
  }
  return -1;
}

static int wirelessStrength(const char *interface, double *rawValue) {
  char contents[16384];
  int strength = -1;
  if (readTextFile("/proc/net/wireless", contents, sizeof(contents)) ||
      parseWirelessQuality(contents, interface, rawValue, &strength))
    return -1;
  return strength;
}

static void findNetworkInterfaces(bool *ethernet,
                                  bool *wifi,
                                  char *wifiInterface,
                                  size_t wifiInterfaceSize) {
  *ethernet = false;
  *wifi = false;
  wifiInterface[0] = '\0';
  char preferredInterface[256];
  defaultRouteInterface(preferredInterface, sizeof(preferredInterface));
  DIR *directory = opendir("/sys/class/net");
  if (!directory)
    return;
  struct dirent *entry;
  while ((entry = readdir(directory))) {
    if (entry->d_name[0] == '.' || !strcmp(entry->d_name, "lo"))
      continue;
    char path[PANEL_PATH_MAX], state[32];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", entry->d_name);
    if (readTextFile(path, state, sizeof(state)) || strcmp(state, "up") != 0)
      continue;
    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", entry->d_name);
    char phy[PANEL_PATH_MAX];
    snprintf(phy, sizeof(phy), "/sys/class/net/%s/phy80211", entry->d_name);
    if (!access(path, F_OK) || !access(phy, F_OK)) {
      *wifi = true;
      if (!wifiInterface[0] || !strcmp(entry->d_name, preferredInterface))
        snprintf(wifiInterface, wifiInterfaceSize, "%s", entry->d_name);
    } else {
      snprintf(path, sizeof(path), "/sys/class/net/%s/device", entry->d_name);
      if (!access(path, F_OK))
        *ethernet = true;
    }
  }
  closedir(directory);
}

int wifiDiagnostic(WifiDiagnostic *diagnostic) {
  if (!diagnostic)
    return -1;
  memset(diagnostic, 0, sizeof(*diagnostic));
  diagnostic->percent = -1;
  bool ethernet = false, wifi = false;
  findNetworkInterfaces(
      &ethernet, &wifi, diagnostic->interface, sizeof(diagnostic->interface));
  (void)ethernet;
  if (!wifi)
    return -1;
  double rawValue = 0.0;
  int kernelStrength = wirelessStrength(diagnostic->interface, &rawValue);
  if (kernelStrength >= 0) {
    snprintf(diagnostic->backend, sizeof(diagnostic->backend), "kernel-proc");
    diagnostic->rawValue = rawValue;
    diagnostic->percent = kernelStrength;
    return 0;
  }
  if (!commandExists("nmcli"))
    return -1;
  char output[2048], ssid[128];
  int nmcliStrength = -1;
  char *arguments[] = {"nmcli",
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
                       diagnostic->interface,
                       NULL};
  if (runCapture(arguments, output, sizeof(output), 1500) ||
      parseNmcliWifi(output, ssid, sizeof(ssid), &nmcliStrength))
    return -1;
  snprintf(diagnostic->backend, sizeof(diagnostic->backend), "nmcli");
  diagnostic->rawValue = nmcliStrength;
  diagnostic->percent = nmcliStrength;
  return 0;
}

void moduleNetwork(const PanelConfig *c, PanelState *s) {
  s->network[0] = '\0';
  if (c->moduleNetwork == MODULE_DISABLED)
    return;
  bool eth = false, wifi = false;
  char ssid[128] = "-", wifiInterface[256] = "";
  int strength = -1;
  findNetworkInterfaces(&eth, &wifi, wifiInterface, sizeof(wifiInterface));
  if (!moduleModeActive(c->moduleNetwork, eth || wifi))
    return;
  double rawKernelStrength = 0.0;
  int kernelStrength =
      wifi ? wirelessStrength(wifiInterface, &rawKernelStrength) : -1;
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
  if (appRoleAvailable(c, APP_ROLE_NETWORK_SETTINGS))
    action(s->network, sizeof(s->network), 1, "role|network_settings", tmp);
  else
    snprintf(s->network, sizeof(s->network), "%s", tmp);
}

void moduleBrightnessValue(const PanelConfig *c, PanelState *s, int pct) {
  s->brightness[0] = '\0';
  if (!moduleModeActive(c->moduleBrightness, true))
    return;
  char text[64], body[256], tmp[512];
  snprintf(text, sizeof(text), " %3d%%", pct);
  block(body, sizeof(body), c->colorBg, c->colorBrightness, text);
  action(tmp, sizeof(tmp), 5, "brightness|down", body);
  action(s->brightness, sizeof(s->brightness), 4, "brightness|up", tmp);
}

bool moduleBrightnessAdjust(const PanelConfig *c,
                            PanelState *s,
                            const char *operation) {
  if (!s->brightnessInitialized || s->brightnessUpdatePending ||
      !s->brightnessOutput[0] || !operation)
    return false;
  int direction = 0;
  if (!strcmp(operation, "up"))
    direction = 1;
  else if (!strcmp(operation, "down"))
    direction = -1;
  else
    return false;
  int target = s->brightnessPercent + direction * c->brightnessStep;
  if (target < 5)
    target = 5;
  if (target > 100)
    target = 100;
  if (target == s->brightnessPercent)
    return false;
  s->brightnessPercent = target;
  moduleBrightnessValue(c, s, target);
  return true;
}

void moduleBrightness(const PanelConfig *c, PanelState *s) {
  s->brightness[0] = '\0';
  s->brightnessInitialized = false;
  s->brightnessOutput[0] = '\0';
  if (!moduleModeActive(c->moduleBrightness, commandExists("xrandr")) ||
      !commandExists("xrandr"))
    return;
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
  s->weather[0] = '\0';
  if (!moduleModeActive(c->moduleWeather, c->location[0] && data[0]))
    return;
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
  char text[96], body[256], right[512], middle[768];
  snprintf(text, sizeof(text), "爫%3d%% %3d° %3d°", rain, min, max);
  block(body, sizeof(body), c->colorBg, c->colorWeather, text);
  if (*c->weatherImage && appCanOpenFile(c->weatherImage))
    action(right, sizeof(right), 3, "weather|open", body);
  else
    snprintf(right, sizeof(right), "%s", body);
  action(middle, sizeof(middle), 2, "weather|refresh", right);
  if (c->weatherLocationCount > 1)
    action(s->weather, sizeof(s->weather), 1, "weather|locations", middle);
  else
    snprintf(s->weather, sizeof(s->weather), "%s", middle);
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

void moduleWorkspaceEwmh(const PanelConfig *c,
                         PanelState *s,
                         const WorkspaceSnapshot *snapshot) {
  s->workspace[0] = '\0';
  s->focusedWorkspaceKnown = false;
  s->focusedWorkspaceOccupied = false;
  if (!snapshot || snapshot->count == 0 ||
      snapshot->count > PANEL_WORKSPACE_MAX ||
      snapshot->current >= snapshot->count)
    return;
  s->focusedWorkspaceKnown = true;
  s->focusedWorkspaceOccupied = snapshot->occupied[snapshot->current];
  for (size_t i = 0; i < snapshot->count; i++) {
    bool focused = i == snapshot->current;
    const char *fg = c->colorFree;
    const char *bg = c->colorFreeBg;
    if (focused && snapshot->urgent[i]) {
      fg = c->colorFocusedUrgent;
      bg = c->colorFocusedUrgentBg;
    } else if (focused && snapshot->occupied[i]) {
      fg = c->colorFocusedOccupied;
      bg = c->colorFocusedOccupiedBg;
    } else if (focused) {
      fg = c->colorFocusedFree;
      bg = c->colorFocusedFreeBg;
    } else if (snapshot->urgent[i]) {
      fg = c->colorUrgent;
      bg = c->colorUrgentBg;
    } else if (snapshot->occupied[i]) {
      fg = c->colorOccupied;
      bg = c->colorOccupiedBg;
    }
    char fallback[16], label[128], part[512];
    snprintf(fallback, sizeof(fallback), "%u", (unsigned)i + 1U);
    shellQuoteAction(snapshot->names[i][0] ? snapshot->names[i] : fallback,
                     label,
                     sizeof(label));
    snprintf(part,
             sizeof(part),
             "%%{F%s}%%{B%s}%%{U%s}%%{+u}%%{A1:workspace|%zu:} %s "
             "%%{A}%%{B-}%%{F-}%%{-u}",
             fg,
             bg,
             c->colorNetwork,
             i,
             label);
    strncat(
        s->workspace, part, sizeof(s->workspace) - strlen(s->workspace) - 1);
  }
}

void moduleStatic(const PanelConfig *c, PanelState *s) {
  char body[128];
  s->launcher[0] = '\0';
  s->power[0] = '\0';
  bool internalLauncher = strcmp(c->applicationLauncher, "external") != 0 &&
                          strcmp(c->applicationLauncher, "disabled") != 0 &&
                          c->internalLauncherAvailable;
  bool externalLauncher = strcmp(c->applicationLauncher, "internal") != 0 &&
                          strcmp(c->applicationLauncher, "disabled") != 0 &&
                          appSpecAvailable(c, c->launcher);
  if (moduleModeActive(c->moduleLauncher,
                       internalLauncher || externalLauncher)) {
    block(body, sizeof(body), c->colorBg, c->colorFg, "");
    action(s->launcher, sizeof(s->launcher), 1, "launcher", body);
  }
  bool internalPower = strcmp(c->powerMenuMode, "external") != 0 &&
                       strcmp(c->powerMenuMode, "disabled") != 0 &&
                       c->internalPowerAvailable;
  bool externalPower = strcmp(c->powerMenuMode, "internal") != 0 &&
                       strcmp(c->powerMenuMode, "disabled") != 0 &&
                       appSpecAvailable(c, c->powerMenu);
  if (moduleModeActive(c->modulePower, internalPower || externalPower)) {
    block(body, sizeof(body), c->colorBg, c->colorFg, "");
    action(s->power, sizeof(s->power), 1, "power", body);
  }
}

void moduleInhibitor(const PanelConfig *c,
                     PanelState *s,
                     bool available,
                     bool active) {
  s->inhibitor[0] = '\0';
  if (!moduleModeActive(c->moduleInhibitor, available) || !available)
    return;
  char body[128];
  block(body,
        sizeof(body),
        c->colorBg,
        active ? c->colorWarning : c->colorFree,
        c->iconFont[0] ? "" : "☕");
  action(s->inhibitor, sizeof(s->inhibitor), 1, "inhibitor|toggle", body);
}

void moduleTimer(const PanelConfig *c,
                 PanelState *s,
                 unsigned minutes,
                 TimerDisplay display,
                 unsigned animationFrame) {
  s->timer[0] = '\0';
  if (!moduleModeActive(c->moduleTimer, true))
    return;
  char text[64], body[128];
  static const char *const ANIMATION_GLYPHS[TIMER_ANIMATION_FRAMES] = {
      "󰪞", "󰪟", "󰪠", "󰪡", "󰪢", "󰪣", "󰪤", "󰪥"};
  const char *glyph = "⏲";
  if (c->iconFont[0]) {
    switch (display) {
    case TIMER_DISPLAY_SET:
      glyph = "󰀡";
      break;
    case TIMER_DISPLAY_RUNNING:
      glyph = ANIMATION_GLYPHS[animationFrame % TIMER_ANIMATION_FRAMES];
      break;
    case TIMER_DISPLAY_PAUSED:
      glyph = "󰚎";
      break;
    case TIMER_DISPLAY_EXPIRED:
      glyph = "󰀢";
      break;
    case TIMER_DISPLAY_RESET:
      glyph = "󰀣";
      break;
    case TIMER_DISPLAY_EMPTY:
    default:
      glyph = "󰀠";
      break;
    }
  }
  bool active = display == TIMER_DISPLAY_SET ||
                display == TIMER_DISPLAY_RUNNING ||
                display == TIMER_DISPLAY_PAUSED;
  if (active)
    snprintf(text, sizeof(text), "%u %s", minutes, glyph);
  else
    snprintf(text, sizeof(text), "%s", glyph);
  block(body,
        sizeof(body),
        c->colorBg,
        active ? c->colorUrgent : c->colorClock,
        text);
  snprintf(s->timer,
           sizeof(s->timer),
           "%%{A1:timer|toggle:}%%{A3:timer|reset:}"
           "%%{A4:timer|up:}%%{A5:timer|down:}%s"
           "%%{A}%%{A}%%{A}%%{A}",
           body);
}

void renderPanel(const PanelState *s, char *out, size_t n) {
  snprintf(out,
           n,
           "%%{l}%s%s%%{c}%s%%{r}%s%s%s%s%s%s%s%s%s%s%s%s\n",
           s->launcher,
           s->workspace,
           s->title,
           s->screencast,
           s->timer,
           s->inhibitor,
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
