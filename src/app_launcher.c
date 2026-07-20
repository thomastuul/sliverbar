#include "app_launcher.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_GIO
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#endif

typedef struct {
  int argc;
  char **argv;
#ifndef HAVE_GIO
  char storage[256];
  char *fallbackArgv[2];
#endif
} ParsedCommand;

typedef struct {
  const char *desktopId;
  const char *command;
  bool terminal;
} Candidate;

static const Candidate SYSTEM_MONITORS[] = {
    {"org.gnome.SystemMonitor.desktop", NULL, false},
    {"org.kde.plasma-systemmonitor.desktop", NULL, false},
    {"xfce4-taskmanager.desktop", NULL, false},
    {"lxtask.desktop", NULL, false},
    {NULL, "btop", true},
    {NULL, "htop", true},
    {NULL, "top", true},
};

static const Candidate NETWORK_SETTINGS[] = {
    {"nm-connection-editor.desktop", NULL, false},
    {NULL, "nm-connection-editor", false},
    {NULL, "nmtui", true},
};

static bool networkManagerActive(void) {
  return commandExists("nmcli") && (!access("/run/NetworkManager", F_OK) ||
                                    !access("/var/run/NetworkManager", F_OK));
}

static const Candidate VOLUME_SETTINGS[] = {
    {"pavucontrol.desktop", NULL, false},
    {NULL, "pavucontrol", false},
    {NULL, "pulsemixer", true},
    {NULL, "alsamixer", true},
};

static const Candidate CALENDARS[] = {
    {"org.gnome.Calendar.desktop", NULL, false},
    {"org.kde.kalendar.desktop", NULL, false},
    {"thunderbird.desktop", NULL, false},
};

static const char *const TERMINALS[] = {"foot",
                                        "alacritty",
                                        "kitty",
                                        "wezterm",
                                        "gnome-terminal",
                                        "konsole",
                                        "xfce4-terminal",
                                        "xterm"};

static bool parseCommand(const char *spec, ParsedCommand *parsed) {
  memset(parsed, 0, sizeof(*parsed));
  const char *command = !strncmp(spec, "command:", 8) ? spec + 8 : spec;
  while (isspace((unsigned char)*command))
    command++;
  if (!*command)
    return false;
#ifdef HAVE_GIO
  GError *error = NULL;
  if (!g_shell_parse_argv(command, &parsed->argc, &parsed->argv, &error)) {
    if (error)
      g_error_free(error);
    return false;
  }
  return parsed->argc > 0 && parsed->argv[0][0];
#else
  if (strpbrk(command, " \t\r\n\"'\\"))
    return false;
  snprintf(parsed->storage, sizeof(parsed->storage), "%s", command);
  parsed->fallbackArgv[0] = parsed->storage;
  parsed->fallbackArgv[1] = NULL;
  parsed->argv = parsed->fallbackArgv;
  parsed->argc = 1;
  return true;
#endif
}

static void freeCommand(ParsedCommand *parsed) {
#ifdef HAVE_GIO
  g_strfreev(parsed->argv);
#else
  (void)parsed;
#endif
}

static bool desktopAvailable(const char *desktopId) {
#ifdef HAVE_GIO
  GDesktopAppInfo *info = g_desktop_app_info_new(desktopId);
  bool available = info != NULL;
  if (info)
    g_object_unref(info);
  return available;
#else
  (void)desktopId;
  return false;
#endif
}

static int launchDesktop(const char *desktopId) {
#ifdef HAVE_GIO
  GDesktopAppInfo *info = g_desktop_app_info_new(desktopId);
  if (!info)
    return -1;
  GError *error = NULL;
  gboolean launched = g_app_info_launch(G_APP_INFO(info), NULL, NULL, &error);
  if (error) {
    logMessage("ERROR", "cannot launch %s: %s", desktopId, error->message);
    g_error_free(error);
  }
  g_object_unref(info);
  return launched ? 0 : -1;
#else
  (void)desktopId;
  return -1;
#endif
}

static bool defaultTypeAvailable(const char *contentType) {
#ifdef HAVE_GIO
  GAppInfo *info = g_app_info_get_default_for_type(contentType, false);
  if (!info)
    return false;
  g_object_unref(info);
  return true;
#else
  (void)contentType;
  return false;
#endif
}

static int launchDefaultType(const char *contentType) {
#ifdef HAVE_GIO
  GAppInfo *info = g_app_info_get_default_for_type(contentType, false);
  if (!info)
    return -1;
  GError *error = NULL;
  gboolean launched = g_app_info_launch(info, NULL, NULL, &error);
  if (error) {
    logMessage("ERROR",
               "cannot launch handler for %s: %s",
               contentType,
               error->message);
    g_error_free(error);
  }
  g_object_unref(info);
  return launched ? 0 : -1;
#else
  (void)contentType;
  return -1;
#endif
}

bool appLauncherHasGio(void) {
#ifdef HAVE_GIO
  return true;
#else
  return false;
#endif
}

const char *appRoleName(AppRole role) {
  switch (role) {
  case APP_ROLE_SYSTEM_MONITOR:
    return "system_monitor";
  case APP_ROLE_NETWORK_SETTINGS:
    return "network_settings";
  case APP_ROLE_VOLUME_SETTINGS:
    return "volume_settings";
  case APP_ROLE_CALENDAR:
    return "calendar";
  }
  return "unknown";
}

static const char *roleSpec(const PanelConfig *config, AppRole role) {
  switch (role) {
  case APP_ROLE_SYSTEM_MONITOR:
    return config->systemMonitor;
  case APP_ROLE_NETWORK_SETTINGS:
    return config->networkSettings;
  case APP_ROLE_VOLUME_SETTINGS:
    return config->volumeSettings;
  case APP_ROLE_CALENDAR:
    return config->calendar;
  }
  return "";
}

static void
roleCandidates(AppRole role, const Candidate **candidates, size_t *count) {
  switch (role) {
  case APP_ROLE_SYSTEM_MONITOR:
    *candidates = SYSTEM_MONITORS;
    *count = sizeof(SYSTEM_MONITORS) / sizeof(SYSTEM_MONITORS[0]);
    break;
  case APP_ROLE_NETWORK_SETTINGS:
    *candidates = NETWORK_SETTINGS;
    *count = sizeof(NETWORK_SETTINGS) / sizeof(NETWORK_SETTINGS[0]);
    break;
  case APP_ROLE_VOLUME_SETTINGS:
    *candidates = VOLUME_SETTINGS;
    *count = sizeof(VOLUME_SETTINGS) / sizeof(VOLUME_SETTINGS[0]);
    break;
  case APP_ROLE_CALENDAR:
    *candidates = CALENDARS;
    *count = sizeof(CALENDARS) / sizeof(CALENDARS[0]);
    break;
  }
}

static const char *desktopIdFromSpec(const char *spec) {
  if (!strncmp(spec, "desktop:", 8))
    return spec + 8;
  size_t length = strlen(spec);
  return length > 8 && !strcmp(spec + length - 8, ".desktop") ? spec : NULL;
}

static const char *terminalExecutable(void);

bool appTerminalAvailable(const PanelConfig *config) {
  if (strcmp(config->terminal, "auto") != 0) {
    ParsedCommand parsed;
    if (!parseCommand(config->terminal, &parsed))
      return false;
    bool available = commandExists(parsed.argv[0]);
    freeCommand(&parsed);
    return available;
  }
  const char *environment = getenv("TERMINAL");
  if (commandExists("xdg-terminal-exec"))
    return true;
  if (environment && *environment && !strpbrk(environment, " \t") &&
      commandExists(environment))
    return true;
  for (size_t i = 0; i < sizeof(TERMINALS) / sizeof(TERMINALS[0]); i++)
    if (commandExists(TERMINALS[i]))
      return true;
  return false;
}

void appDescribeTerminal(const PanelConfig *config,
                         char *output,
                         size_t outputSize) {
  if (strcmp(config->terminal, "auto") != 0) {
    snprintf(output,
             outputSize,
             "%s",
             appTerminalAvailable(config) ? config->terminal : "unavailable");
    return;
  }
  const char *terminal = terminalExecutable();
  snprintf(output, outputSize, "%s", terminal ? terminal : "unavailable");
}

static const char *terminalExecutable(void) {
  if (commandExists("xdg-terminal-exec"))
    return "xdg-terminal-exec";
  const char *environment = getenv("TERMINAL");
  if (environment && *environment && !strpbrk(environment, " \t") &&
      commandExists(environment))
    return environment;
  for (size_t i = 0; i < sizeof(TERMINALS) / sizeof(TERMINALS[0]); i++)
    if (commandExists(TERMINALS[i]))
      return TERMINALS[i];
  return NULL;
}

static const char *terminalSeparator(const char *executable) {
  const char *base = strrchr(executable, '/');
  base = base ? base + 1 : executable;
  if (!strcmp(base, "xdg-terminal-exec") || !strcmp(base, "kitty"))
    return NULL;
  if (!strcmp(base, "wezterm"))
    return "start";
  if (!strcmp(base, "gnome-terminal"))
    return "--";
  if (!strcmp(base, "xfce4-terminal"))
    return "-x";
  return "-e";
}

int appLaunchTerminal(const PanelConfig *config, char *const command[]) {
  ParsedCommand configured = {0};
  const char *terminal = NULL;
  bool parsed = false;
  if (strcmp(config->terminal, "auto") != 0) {
    parsed = parseCommand(config->terminal, &configured);
    if (!parsed || !commandExists(configured.argv[0])) {
      freeCommand(&configured);
      return -1;
    }
    terminal = configured.argv[0];
  } else {
    terminal = terminalExecutable();
  }
  if (!terminal)
    return -1;

  char *argv[PANEL_ARG_MAX * 2] = {0};
  const size_t CAPACITY = sizeof(argv) / sizeof(argv[0]);
  size_t used = 0;
  if (parsed) {
    for (int i = 0; i < configured.argc && used + 1 < CAPACITY; i++)
      argv[used++] = configured.argv[i];
  } else {
    argv[used++] = (char *)terminal;
  }
  const char *separator = terminalSeparator(terminal);
  if (separator && used + 1 < CAPACITY)
    argv[used++] = (char *)separator;
  if (separator && !strcmp(separator, "start") && used + 1 < CAPACITY)
    argv[used++] = "--";
  for (size_t i = 0; command[i] && used + 1 < CAPACITY; i++)
    argv[used++] = command[i];
  argv[used] = NULL;
  int result = spawnDetached(argv);
  if (parsed)
    freeCommand(&configured);
  return result;
}

bool appSpecAvailable(const PanelConfig *config, const char *spec) {
  if (!spec || !*spec || !strcmp(spec, "auto") || !strcmp(spec, "disabled"))
    return false;
  const char *desktopId = desktopIdFromSpec(spec);
  if (desktopId)
    return desktopAvailable(desktopId);
  bool terminal = !strncmp(spec, "terminal:", 9);
  ParsedCommand parsed;
  if (!parseCommand(terminal ? spec + 9 : spec, &parsed))
    return false;
  bool available = commandExists(parsed.argv[0]) &&
                   (!terminal || appTerminalAvailable(config));
  freeCommand(&parsed);
  return available;
}

int appLaunchSpec(const PanelConfig *config, const char *spec) {
  const char *desktopId = desktopIdFromSpec(spec);
  if (desktopId)
    return launchDesktop(desktopId);
  bool terminal = !strncmp(spec, "terminal:", 9);
  ParsedCommand parsed;
  if (!parseCommand(terminal ? spec + 9 : spec, &parsed) ||
      !commandExists(parsed.argv[0])) {
    freeCommand(&parsed);
    return -1;
  }
  int result = terminal ? appLaunchTerminal(config, parsed.argv)
                        : spawnDetached(parsed.argv);
  freeCommand(&parsed);
  return result;
}

static bool candidateAvailable(const PanelConfig *config,
                               const Candidate *candidate) {
  if (candidate->desktopId)
    return desktopAvailable(candidate->desktopId);
  return commandExists(candidate->command) &&
         (!candidate->terminal || appTerminalAvailable(config));
}

bool appRoleAvailable(const PanelConfig *config, AppRole role) {
  const char *spec = roleSpec(config, role);
  if (!strcmp(spec, "disabled") || !*spec)
    return false;
  if (strcmp(spec, "auto") != 0)
    return appSpecAvailable(config, spec);
  if (role == APP_ROLE_CALENDAR && defaultTypeAvailable("text/calendar"))
    return true;
  if (role == APP_ROLE_NETWORK_SETTINGS && !networkManagerActive())
    return false;
  const Candidate *candidates = NULL;
  size_t count = 0;
  roleCandidates(role, &candidates, &count);
  for (size_t i = 0; i < count; i++)
    if (candidateAvailable(config, &candidates[i]))
      return true;
  return false;
}

int appLaunchRole(const PanelConfig *config, AppRole role) {
  const char *spec = roleSpec(config, role);
  if (!strcmp(spec, "disabled") || !*spec)
    return -1;
  if (strcmp(spec, "auto") != 0)
    return appLaunchSpec(config, spec);
  if (role == APP_ROLE_CALENDAR && defaultTypeAvailable("text/calendar"))
    return launchDefaultType("text/calendar");
  if (role == APP_ROLE_NETWORK_SETTINGS && !networkManagerActive())
    return -1;
  const Candidate *candidates = NULL;
  size_t count = 0;
  roleCandidates(role, &candidates, &count);
  for (size_t i = 0; i < count; i++) {
    if (!candidateAvailable(config, &candidates[i]))
      continue;
    if (candidates[i].desktopId)
      return launchDesktop(candidates[i].desktopId);
    char *argv[] = {(char *)candidates[i].command, NULL};
    return candidates[i].terminal ? appLaunchTerminal(config, argv)
                                  : spawnDetached(argv);
  }
  return -1;
}

void appDescribeRole(const PanelConfig *config,
                     AppRole role,
                     char *output,
                     size_t outputSize) {
  const char *spec = roleSpec(config, role);
  if (!spec || !*spec || !strcmp(spec, "disabled")) {
    snprintf(output, outputSize, "disabled");
    return;
  }
  if (strcmp(spec, "auto") != 0) {
    snprintf(output,
             outputSize,
             "%s%s",
             appSpecAvailable(config, spec) ? "override:" : "unavailable:",
             spec);
    return;
  }
  if (role == APP_ROLE_CALENDAR && defaultTypeAvailable("text/calendar")) {
    snprintf(output, outputSize, "mime:text/calendar");
    return;
  }
  if (role == APP_ROLE_NETWORK_SETTINGS && !networkManagerActive()) {
    snprintf(output, outputSize, "unavailable:no-active-NetworkManager");
    return;
  }
  const Candidate *candidates = NULL;
  size_t count = 0;
  roleCandidates(role, &candidates, &count);
  for (size_t i = 0; i < count; i++) {
    if (!candidateAvailable(config, &candidates[i]))
      continue;
    snprintf(output,
             outputSize,
             "%s:%s",
             candidates[i].desktopId  ? "desktop"
             : candidates[i].terminal ? "terminal"
                                      : "command",
             candidates[i].desktopId ? candidates[i].desktopId
                                     : candidates[i].command);
    return;
  }
  snprintf(output, outputSize, "unavailable");
}

bool appCanOpenFile(const char *path) {
#ifdef HAVE_GIO
  GFile *file = g_file_new_for_path(path);
  GAppInfo *info = g_file_query_default_handler(file, NULL, NULL);
  g_object_unref(file);
  if (!info)
    return false;
  g_object_unref(info);
  return true;
#else
  return commandExists("xdg-open") && path && *path;
#endif
}

int appOpenFile(const char *path) {
#ifdef HAVE_GIO
  char *uri = g_filename_to_uri(path, NULL, NULL);
  if (!uri)
    return -1;
  GError *error = NULL;
  gboolean launched = g_app_info_launch_default_for_uri(uri, NULL, &error);
  if (error) {
    logMessage("ERROR", "cannot open %s: %s", path, error->message);
    g_error_free(error);
  }
  g_free(uri);
  return launched ? 0 : -1;
#else
  char *argv[] = {"xdg-open", (char *)path, NULL};
  return commandExists("xdg-open") ? spawnDetached(argv) : -1;
#endif
}

#ifdef HAVE_GIO
static int compareAppEntries(const void *left, const void *right) {
  const AppEntry *a = left;
  const AppEntry *b = right;
  int result = g_utf8_collate(a->name, b->name);
  return result ? result : strcmp(a->desktopId, b->desktopId);
}
#endif

size_t appCatalogLoad(AppEntry *entries, size_t capacity) {
  if (!entries || capacity == 0)
    return 0;
#ifdef HAVE_GIO
  GList *applications = g_app_info_get_all();
  size_t count = 0;
  for (GList *node = applications; node && count < capacity;
       node = node->next) {
    GAppInfo *info = G_APP_INFO(node->data);
    const char *id = g_app_info_get_id(info);
    if (!id || !*id || !g_app_info_should_show(info))
      continue;
    const char *name = g_app_info_get_display_name(info);
    const char *generic = g_app_info_get_name(info);
    const char *description = g_app_info_get_description(info);
    const char *executable = g_app_info_get_executable(info);
    snprintf(entries[count].name,
             sizeof(entries[count].name),
             "%s",
             name && *name ? name : id);
    snprintf(
        entries[count].desktopId, sizeof(entries[count].desktopId), "%s", id);
    snprintf(entries[count].search,
             sizeof(entries[count].search),
             "%s %s %s %s %s",
             name ? name : "",
             generic ? generic : "",
             description ? description : "",
             id,
             executable ? executable : "");
    count++;
  }
  g_list_free_full(applications, g_object_unref);
  qsort(entries, count, sizeof(*entries), compareAppEntries);
  return count;
#else
  (void)capacity;
  return 0;
#endif
}

int appSearchRank(const char *label, const char *search, const char *query) {
  if (!label || !search || !query)
    return -1;
  if (!*query)
    return 3;
#ifdef HAVE_GIO
  char *foldedLabel = g_utf8_casefold(label, -1);
  char *foldedSearch = g_utf8_casefold(search, -1);
  char *foldedQuery = g_utf8_casefold(query, -1);
  char **tokens = g_strsplit_set(foldedQuery, " \t", -1);
  bool matches = true;
  for (size_t i = 0; tokens[i]; i++)
    if (*tokens[i] && !strstr(foldedSearch, tokens[i])) {
      matches = false;
      break;
    }
  int rank = -1;
  if (matches) {
    if (g_str_has_prefix(foldedLabel, foldedQuery)) {
      rank = 0;
    } else {
      const char *match = foldedLabel;
      while ((match = strstr(match, foldedQuery))) {
        if (match == foldedLabel ||
            g_unichar_isspace(
                g_utf8_get_char(g_utf8_find_prev_char(foldedLabel, match))) ||
            strchr("-_/.", match[-1])) {
          rank = 1;
          break;
        }
        match++;
      }
      if (rank < 0)
        rank = 2;
    }
  }
  g_strfreev(tokens);
  g_free(foldedQuery);
  g_free(foldedSearch);
  g_free(foldedLabel);
  return rank;
#else
  return strstr(search, query) ? (strncmp(label, query, strlen(query)) ? 2 : 0)
                               : -1;
#endif
}
