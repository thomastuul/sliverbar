#include "panel.h"

#include "agenda.h"
#include "app_launcher.h"
#include "control_ipc.h"
#include "inhibitor.h"
#include "power_actions.h"
#include "power_profiles.h"
#include "timer.h"
#include "weather_forecast.h"

#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr,                                                          \
              "check failed at %s:%d: %s\n",                                   \
              __FILE__,                                                        \
              __LINE__,                                                        \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

typedef struct {
  PowerProfileState state;
  bool reject;
  unsigned setCalls;
} FakePowerProfiles;

static int fakePowerProfilesQuery(const PanelConfig *config,
                                  PowerProfileState *state,
                                  void *context) {
  (void)config;
  FakePowerProfiles *fake = context;
  *state = fake->state;
  return state->available ? 0 : -1;
}

static int fakePowerProfileSet(const char *id,
                               char *error,
                               size_t errorSize,
                               void *context) {
  FakePowerProfiles *fake = context;
  fake->setCalls++;
  if (fake->reject) {
    snprintf(error, errorSize, "authorization rejected");
    return -1;
  }
  snprintf(fake->state.active, sizeof(fake->state.active), "%s", id);
  return 0;
}

static time_t localTime(int year, int month, int day, int hour, int minute) {
  struct tm value = {
      .tm_year = year - 1900,
      .tm_mon = month - 1,
      .tm_mday = day,
      .tm_hour = hour,
      .tm_min = minute,
      .tm_isdst = -1,
  };
  return mktime(&value);
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--inhibit-holder") == 0) {
    for (;;)
      pause();
  }
  if (argc == 3 && strcmp(argv[1], "--signal-probe") == 0) {
    sigset_t mask;
    struct sigaction action;
    sigprocmask(SIG_SETMASK, NULL, &mask);
    sigaction(SIGPIPE, NULL, &action);
    FILE *probe = fopen(argv[2], "w");
    if (!probe)
      return 2;
    fputs(!sigismember(&mask, SIGCHLD) && action.sa_handler == SIG_DFL ? "ok"
                                                                       : "bad",
          probe);
    return fclose(probe) == 0 ? 0 : 2;
  }
  if (argc == 2 && strcmp(argv[1], "--locale-probe") == 0) {
    const char *locale = getenv("LC_ALL");
    fputs(locale ? locale : "", stdout);
    return 0;
  }

  const char *originalLocale = getenv("LC_ALL");
  char savedLocale[128] = "";
  if (originalLocale)
    snprintf(savedLocale, sizeof(savedLocale), "%s", originalLocale);
  CHECK(setenv("LC_ALL", "de_DE.UTF-8", 1) == 0);
  char localeOutput[32];
  char *localeProbe[] = {argv[0], "--locale-probe", NULL};
  CHECK(runCapture(localeProbe, localeOutput, sizeof(localeOutput), 1000) == 0);
  CHECK(strcmp(localeOutput, "C") == 0);
  CHECK(originalLocale ? setenv("LC_ALL", savedLocale, 1) == 0
                       : unsetenv("LC_ALL") == 0);

  char controlAction[CONTROL_ACTION_MAX];
  CHECK(
      controlActionBuild("volume", "up", controlAction, sizeof(controlAction)));
  CHECK(strcmp(controlAction, "volume|up") == 0);
  CHECK(controlActionBuild(
      "brightness", "down", controlAction, sizeof(controlAction)));
  CHECK(strcmp(controlAction, "brightness|down") == 0);
  CHECK(controlActionBuild(
      "refresh", "volume", controlAction, sizeof(controlAction)));
  CHECK(strcmp(controlAction, "refresh|volume") == 0);
  CHECK(!controlActionBuild(
      "power_action", "poweroff", controlAction, sizeof(controlAction)));
  CHECK(controlActionValid("volume|toggle"));
  CHECK(controlActionValid("refresh|brightness"));
  CHECK(!controlActionValid("volume|invalid"));
  CHECK(!controlActionValid("power_action|poweroff"));
  CHECK(!controlActionValid("volume|up|extra"));

  char controlDirectory[] = "/tmp/sliverbar-control-XXXXXX";
  CHECK(mkdtemp(controlDirectory) != NULL);
  char controlPath[PANEL_PATH_MAX];
  CHECK(snprintf(controlPath,
                 sizeof(controlPath),
                 "%s/control.sock",
                 controlDirectory) > 0);
  int controlFd = controlServerOpen(controlPath);
  CHECK(controlFd >= 0);
  CHECK(controlClientSend(controlPath, "volume|up") == 0);
  char receivedAction[CONTROL_ACTION_MAX];
  CHECK(controlServerReceive(
            controlFd, receivedAction, sizeof(receivedAction)) > 0);
  CHECK(strcmp(receivedAction, "volume|up") == 0);
  controlServerClose(controlFd, controlPath);
  CHECK(access(controlPath, F_OK) != 0);
  CHECK(rmdir(controlDirectory) == 0);

  PanelConfig cfg;
  configDefaults(&cfg);
  CHECK(cfg.height == 25);
  CHECK(cfg.blockPadding == 6);
  CHECK(cfg.volumeStep == 2);
  CHECK(strcmp(cfg.colorPanelBg, "#191A21") == 0);
  CHECK(strcmp(cfg.colorBg, "#282A36") == 0);
  CHECK(strcmp(cfg.wmName, "sliverbar") == 0);
  CHECK(strcmp(cfg.workspaceBackend, "auto") == 0);
  CHECK(strcmp(cfg.applicationLauncher, "auto") == 0);
  CHECK(strcmp(cfg.powerMenuMode, "auto") == 0);
  CHECK(powerActionAllowed(cfg.powerActions, "reboot"));
  CHECK(!powerActionAllowed(cfg.powerConfirm, "lock"));
  CHECK(strstr(cfg.font, "Monospace") != NULL);
  CHECK(cfg.iconFont[0] == '\0');
  CHECK(strcmp(cfg.terminal, "auto") == 0);
  CHECK(strcmp(cfg.language, "auto") == 0);
  CHECK(strcmp(cfg.tasks, "auto") == 0);
  CHECK(strcmp(cfg.agendaProvider, "none") == 0);
  CHECK(cfg.agendaCalendarSourceMode == AGENDA_SOURCES_ALL);
  CHECK(cfg.agendaTaskSourceMode == AGENDA_SOURCES_ALL);
  CHECK(cfg.agendaDays == 7);
  CHECK(cfg.agendaMaxItems == 10);
  CHECK(cfg.agendaMaxUndatedTasks == 2);
  CHECK(cfg.agendaRefreshInterval == 300);
  CHECK(cfg.agendaPopupWidth == 720);
  CHECK(cfg.agendaShowSource);
  CHECK(strcmp(cfg.agendaEventColor, "#8BE9FD") == 0);
  CHECK(strcmp(cfg.agendaTaskColor, "#FFB86C") == 0);
  CHECK(strcmp(cfg.agendaOverdueColor, "#FF5555") == 0);
  CHECK(strcmp(cfg.agendaSourceColor, "#6272A4") == 0);
  CHECK(strstr(cfg.timerSound, "alarm-clock-elapsed.oga") != NULL);
  CHECK(cfg.location[0] == '\0');
  CHECK(cfg.moduleClock == MODULE_AUTO);
  CHECK(moduleModeActive(MODULE_AUTO, true));
  CHECK(!moduleModeActive(MODULE_AUTO, false));
  CHECK(moduleModeActive(MODULE_ENABLED, false));
  CHECK(!moduleModeActive(MODULE_DISABLED, true));
  const char *volume84 = "Volume: front-left: 55050 /  84% / -4.54 dB,   "
                         "front-right: 55050 /  84% / -4.54 dB";
  const char *volume99 = "Volume: front-left: 64881 /  99% / -0.17 dB,   "
                         "front-right: 64881 /  99% / -0.17 dB";
  const char *volume100 = "Volume: front-left: 65536 / 100% / 0.00 dB,   "
                          "front-right: 65536 / 100% / 0.00 dB";
  const char *volume125 = "Volume: front-left: 81920 / 125% / 5.81 dB,   "
                          "front-right: 81920 / 125% / 5.81 dB";
  char volumeArgument[16];
  CHECK(pactlVolumeArgument(
      volume84, 1, "up", volumeArgument, sizeof(volumeArgument)));
  CHECK(strcmp(volumeArgument, "+1%") == 0);
  CHECK(pactlVolumeArgument(
      volume99, 1, "up", volumeArgument, sizeof(volumeArgument)));
  CHECK(strcmp(volumeArgument, "100%") == 0);
  CHECK(pactlVolumeArgument(
      volume100, 1, "up", volumeArgument, sizeof(volumeArgument)));
  CHECK(strcmp(volumeArgument, "100%") == 0);
  CHECK(pactlVolumeArgument(
      volume125, 1, "up", volumeArgument, sizeof(volumeArgument)));
  CHECK(strcmp(volumeArgument, "100%") == 0);
  CHECK(pactlVolumeArgument(
      NULL, 2, "down", volumeArgument, sizeof(volumeArgument)));
  CHECK(strcmp(volumeArgument, "-2%") == 0);
  CHECK(!pactlVolumeArgument(
      "invalid", 1, "up", volumeArgument, sizeof(volumeArgument)));
  CHECK(!pactlVolumeArgument(
      volume84, 0, "up", volumeArgument, sizeof(volumeArgument)));
  CHECK(!pactlVolumeArgument(
      volume84, 1, "invalid", volumeArgument, sizeof(volumeArgument)));
  snprintf(cfg.language, sizeof(cfg.language), "de");
  CHECK(panelLanguageIsGerman(&cfg));
  CHECK(strcmp(powerActionLabel(&cfg, "poweroff"), "Ausschalten") == 0);
  snprintf(cfg.language, sizeof(cfg.language), "en");
  CHECK(!panelLanguageIsGerman(&cfg));
  CHECK(strcmp(powerActionLabel(&cfg, "poweroff"), "Power off") == 0);
  snprintf(cfg.language, sizeof(cfg.language), "auto");

  CHECK(setenv("TZ", "UTC", 1) == 0);
  tzset();
  time_t agendaNow = localTime(2026, 7, 23, 10, 0);
  AgendaSnapshot agendaSnapshot = {
      .selectedSourceCount = 2,
      .reachableSourceCount = 2,
      .initialized = true,
  };
  AgendaItem agendaItems[] = {
      {.type = AGENDA_ITEM_EVENT,
       .title = "Afternoon",
       .start = localTime(2026, 7, 23, 14, 0),
       .end = localTime(2026, 7, 23, 15, 0),
       .hasStart = true,
       .hasEnd = true},
      {.type = AGENDA_ITEM_TASK,
       .title = "Overdue",
       .due = localTime(2026, 7, 22, 0, 0),
       .hasDue = true,
       .dateOnly = true},
      {.type = AGENDA_ITEM_EVENT,
       .title = "Ongoing",
       .start = localTime(2026, 7, 23, 9, 0),
       .end = localTime(2026, 7, 23, 11, 0),
       .hasStart = true,
       .hasEnd = true},
      {.type = AGENDA_ITEM_TASK,
       .title = "Today",
       .due = localTime(2026, 7, 23, 0, 0),
       .hasDue = true,
       .dateOnly = true},
      {.type = AGENDA_ITEM_TASK, .title = "Undated A"},
      {.type = AGENDA_ITEM_TASK, .title = "Undated B"},
      {.type = AGENDA_ITEM_TASK, .title = "Undated C"},
      {.type = AGENDA_ITEM_EVENT,
       .title = "Cancelled",
       .start = localTime(2026, 7, 23, 12, 0),
       .end = localTime(2026, 7, 23, 13, 0),
       .hasStart = true,
       .hasEnd = true,
       .cancelled = true},
  };
  agendaSnapshot.count = sizeof(agendaItems) / sizeof(agendaItems[0]);
  memcpy(agendaSnapshot.items, agendaItems, sizeof(agendaItems));
  AgendaView agendaView;
  agendaBuildView(&agendaSnapshot, agendaNow, 7, 6, 2, true, &agendaView);
  CHECK(agendaView.initialized);
  CHECK(agendaView.available);
  CHECK(agendaView.count == 6);
  CHECK(strcmp(agendaView.items[0].item.title, "Overdue") == 0);
  CHECK(agendaView.items[0].overdue);
  CHECK(strstr(agendaView.items[0].when, "Ueberfaellig") != NULL);
  CHECK(strcmp(agendaView.items[1].item.title, "Ongoing") == 0);
  CHECK(agendaView.items[1].ongoing);
  CHECK(strstr(agendaView.items[1].when, "Laufend") != NULL);
  CHECK(strcmp(agendaView.items[2].item.title, "Today") == 0);
  CHECK(strcmp(agendaView.items[3].item.title, "Afternoon") == 0);
  CHECK(agendaView.hiddenTasks == 1);
  agendaSnapshot.reachableSourceCount = 0;
  agendaBuildView(&agendaSnapshot, agendaNow, 7, 10, 2, false, &agendaView);
  CHECK(!agendaView.available);
  agendaSnapshot.reachableSourceCount = 1;
  agendaSnapshot.count = 0;
  agendaBuildView(&agendaSnapshot, agendaNow, 7, 10, 2, false, &agendaView);
  CHECK(agendaView.available);
  CHECK(agendaView.count == 0);

  char forecastPath[PANEL_PATH_MAX];
  CHECK(snprintf(forecastPath,
                 sizeof(forecastPath),
                 "%s/tests/fixtures/weather-forecast.json",
                 SLIVERBAR_TEST_SOURCE_DIR) > 0);
  char forecastJson[32768];
  CHECK(readTextFile(forecastPath, forecastJson, sizeof(forecastJson)) == 0);
  WeatherForecast forecast;
  CHECK(weatherForecastParse(forecastJson, &forecast) == 0);
  CHECK(forecast.dayCount == WEATHER_FORECAST_DAY_COUNT);
  CHECK(strcmp(forecast.days[0].date, "2026-07-23") == 0);
  CHECK(forecast.days[0].minimumValid);
  CHECK(forecast.days[0].minimumC == -3);
  CHECK(forecast.days[0].maximumValid);
  CHECK(forecast.days[0].maximumC == 12);
  for (size_t i = 0; i < WEATHER_FORECAST_SLOT_COUNT; i++)
    CHECK(forecast.days[0].slots[i].hour == 6 + (int)i * 3);
  CHECK(forecast.days[0].slots[0].temperatureValid);
  CHECK(forecast.days[0].slots[0].temperatureC == -3);
  CHECK(forecast.days[0].slots[0].rainValid);
  CHECK(forecast.days[0].slots[0].rainPercent == 0);
  CHECK(forecast.days[0].slots[3].rainPercent == 100);
  CHECK(forecast.days[0].slots[0].condition == WEATHER_CONDITION_CLEAR);
  CHECK(forecast.days[0].slots[1].condition == WEATHER_CONDITION_CLOUDY);
  CHECK(forecast.days[0].slots[2].condition == WEATHER_CONDITION_RAIN);
  CHECK(forecast.days[0].slots[3].condition == WEATHER_CONDITION_THUNDER);
  CHECK(forecast.days[0].slots[4].condition == WEATHER_CONDITION_SNOW);
  CHECK(forecast.days[0].slots[5].condition == WEATHER_CONDITION_FOG);
  CHECK(!forecast.days[1].slots[1].temperatureValid);
  CHECK(forecast.days[1].slots[1].rainValid);
  CHECK(forecast.days[1].slots[1].condition == WEATHER_CONDITION_UNKNOWN);
  CHECK(!forecast.days[1].slots[2].rainValid);
  CHECK(!forecast.days[1].slots[3].codeValid);
  char weekday[32];
  CHECK(strcmp(weatherForecastDayName(
                   "2026-07-23", true, weekday, sizeof(weekday)),
               "Donnerstag") == 0);
  CHECK(strcmp(weatherForecastDayName(
                   "2026-07-23", false, weekday, sizeof(weekday)),
               "Thursday") == 0);
  CHECK(
      strcmp(weatherForecastDayName("invalid", true, weekday, sizeof(weekday)),
             "-") == 0);
  CHECK(strcmp(weatherConditionGlyph(WEATHER_CONDITION_CLEAR, false), "☀") ==
        0);
  CHECK(strcmp(weatherConditionGlyph(WEATHER_CONDITION_UNKNOWN, true), "?") ==
        0);
  char updated[64];
  const char *oldTimezone = getenv("TZ");
  char oldTimezoneCopy[128] = "";
  if (oldTimezone)
    snprintf(oldTimezoneCopy, sizeof(oldTimezoneCopy), "%s", oldTimezone);
  CHECK(setenv("TZ", "UTC", 1) == 0);
  tzset();
  const time_t SAME_DAY_UPDATE = 1784797680;
  const time_t PREVIOUS_DAY_UPDATE = 1784745600;
  const time_t CURRENT_TIME = 1784808000;
  CHECK(strcmp(weatherForecastUpdatedLabel(SAME_DAY_UPDATE,
                                           true,
                                           true,
                                           CURRENT_TIME,
                                           updated,
                                           sizeof(updated)),
               "Aktualisiert 09:08") == 0);
  CHECK(strcmp(weatherForecastUpdatedLabel(PREVIOUS_DAY_UPDATE,
                                           true,
                                           true,
                                           CURRENT_TIME,
                                           updated,
                                           sizeof(updated)),
               "Aktualisiert 22.07. 18:40") == 0);
  CHECK(strcmp(weatherForecastUpdatedLabel(SAME_DAY_UPDATE,
                                           true,
                                           false,
                                           CURRENT_TIME,
                                           updated,
                                           sizeof(updated)),
               "Updated 09:08") == 0);
  CHECK(strcmp(weatherForecastUpdatedLabel(
                   0, false, false, CURRENT_TIME, updated, sizeof(updated)),
               "Updated –") == 0);
  if (oldTimezone)
    CHECK(setenv("TZ", oldTimezoneCopy, 1) == 0);
  else
    CHECK(unsetenv("TZ") == 0);
  tzset();
  CHECK(weatherForecastParse("{\"weather\":[]}", &forecast) != 0);
  CHECK(forecast.dayCount == 0);
  CHECK(weatherForecastParse("{\"weather\":[", &forecast) != 0);

  PanelConfig forecastConfig;
  configDefaults(&forecastConfig);
  snprintf(forecastConfig.location,
           sizeof(forecastConfig.location),
           "%s",
           "Test location");
  snprintf(forecastConfig.weatherCache,
           sizeof(forecastConfig.weatherCache),
           "%s",
           forecastPath);
  forecastConfig.moduleWeather = MODULE_ENABLED;
  forecastConfig.internalWeatherForecastAvailable = true;
  PanelState forecastState = {0};
  moduleWeather(&forecastConfig, &forecastState);
  CHECK(strstr(forecastState.weather, "weather|forecast") != NULL);
  CHECK(strstr(forecastState.weather, "weather|open") == NULL);
  CHECK(strstr(forecastState.weather, "爫%{O4}") != NULL);
  CHECK(strstr(forecastState.weather, "%{O8}%{O4}") != NULL);
  CHECK(strstr(forecastState.weather, "%{O8}%{O4}") != NULL);
  forecastConfig.internalWeatherForecastAvailable = false;
  moduleWeather(&forecastConfig, &forecastState);
  CHECK(strstr(forecastState.weather, "weather|forecast") == NULL);
  snprintf(forecastConfig.location,
           sizeof(forecastConfig.location),
           "%s",
           "Hiiumaa, Estonia");
  snprintf(forecastConfig.weatherCache,
           sizeof(forecastConfig.weatherCache),
           "%s",
           "/tmp/sliverbar-weather-cache-does-not-exist");
  forecastConfig.moduleWeather = MODULE_AUTO;
  forecastConfig.weatherLocationCount = 2;
  moduleWeather(&forecastConfig, &forecastState);
  CHECK(forecastState.weather[0] != '\0');
  CHECK(strstr(forecastState.weather, "weather|locations") != NULL);
  CHECK(strstr(forecastState.weather, "weather|refresh") != NULL);
  CHECK(strstr(forecastState.weather, "—") != NULL);
  forecastConfig.location[0] = '\0';
  moduleWeather(&forecastConfig, &forecastState);
  CHECK(forecastState.weather[0] == '\0');
  PanelState agendaClockState = {0};
  snprintf(forecastConfig.calendar,
           sizeof(forecastConfig.calendar),
           "%s",
           "command:/bin/true");
  forecastConfig.internalAgendaAvailable = true;
  moduleClock(&forecastConfig, &agendaClockState);
  CHECK(strstr(agendaClockState.clock, "role|calendar") != NULL);
  CHECK(strstr(agendaClockState.clock, "agenda|toggle") != NULL);
  CHECK(strstr(agendaClockState.clock, "%{O4}") != NULL);
  const char *clockPadding = strstr(agendaClockState.clock, "%{O8}");
  CHECK(clockPadding != NULL);
  CHECK(strstr(clockPadding + 1, "%{O8}") != NULL);
  forecastConfig.internalAgendaAvailable = false;
  moduleClock(&forecastConfig, &agendaClockState);
  CHECK(strstr(agendaClockState.clock, "agenda|toggle") == NULL);

  const char *oldLanguage = getenv("LANGUAGE");
  const char *oldLcMessages = getenv("LC_MESSAGES");
  const char *oldLcAll = getenv("LC_ALL");
  const char *oldLang = getenv("LANG");
  char oldLanguageCopy[128] = "", oldLcMessagesCopy[128] = "";
  char oldLcAllCopy[128] = "", oldLangCopy[128] = "";
  if (oldLanguage)
    snprintf(oldLanguageCopy, sizeof(oldLanguageCopy), "%s", oldLanguage);
  if (oldLcMessages)
    snprintf(oldLcMessagesCopy, sizeof(oldLcMessagesCopy), "%s", oldLcMessages);
  if (oldLcAll)
    snprintf(oldLcAllCopy, sizeof(oldLcAllCopy), "%s", oldLcAll);
  if (oldLang)
    snprintf(oldLangCopy, sizeof(oldLangCopy), "%s", oldLang);
  CHECK(unsetenv("LANGUAGE") == 0);
  CHECK(unsetenv("LC_MESSAGES") == 0);
  CHECK(setenv("LC_ALL", "C.UTF-8", 1) == 0);
  CHECK(setenv("LANG", "de_DE.UTF-8", 1) == 0);
  CHECK(panelLanguageIsGerman(&cfg));
  CHECK(setenv("LANG", "et_EE.UTF-8", 1) == 0);
  CHECK(!panelLanguageIsGerman(&cfg));
  if (oldLanguage)
    CHECK(setenv("LANGUAGE", oldLanguageCopy, 1) == 0);
  else
    CHECK(unsetenv("LANGUAGE") == 0);
  if (oldLcMessages)
    CHECK(setenv("LC_MESSAGES", oldLcMessagesCopy, 1) == 0);
  else
    CHECK(unsetenv("LC_MESSAGES") == 0);
  if (oldLcAll)
    CHECK(setenv("LC_ALL", oldLcAllCopy, 1) == 0);
  else
    CHECK(unsetenv("LC_ALL") == 0);
  if (oldLang)
    CHECK(setenv("LANG", oldLangCopy, 1) == 0);
  else
    CHECK(unsetenv("LANG") == 0);

  char path[] = "/tmp/sliverbar-test-XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  const char TEXT[] =
      "height=31\nblock_padding=9\nvolume_step=4\ncolor_bg=#000000\n"
      "module_cpu=disabled\n"
      "power_actions=lock,reboot,poweroff\npower_confirm=reboot,poweroff\n"
      "module_timer=enabled\ntimer_sound=/tmp/timer.oga\n";
  CHECK(write(fd, TEXT, sizeof(TEXT) - 1) == (ssize_t)(sizeof(TEXT) - 1));
  CHECK(close(fd) == 0);

  char error[256];
  CHECK(configLoad(&cfg, path, error, sizeof(error)) == 0);
  CHECK(cfg.height == 31);
  CHECK(cfg.blockPadding == 9);
  CHECK(cfg.volumeStep == 4);
  CHECK(strcmp(cfg.colorBg, "#000000") == 0);
  CHECK(strcmp(cfg.colorOccupied, "#ff5555") == 0);
  CHECK(cfg.moduleCpu == MODULE_DISABLED);
  CHECK(cfg.moduleTimer == MODULE_ENABLED);
  CHECK(strcmp(cfg.timerSound, "/tmp/timer.oga") == 0);
  CHECK(powerActionAllowed(cfg.powerActions, "reboot"));
  CHECK(!powerActionAllowed(cfg.powerActions, "hibernate"));
  CHECK(unlink(path) == 0);

  PanelConfig agendaConfig;
  configDefaults(&agendaConfig);
  char agendaPath[] = "/tmp/sliverbar-agenda-test-XXXXXX";
  fd = mkstemp(agendaPath);
  CHECK(fd >= 0);
  const char AGENDA_TEXT[] = "agenda_provider=eds\n"
                             "agenda_calendar_source=personal\n"
                             "agenda_calendar_source=work\n"
                             "agenda_task_source=none\n"
                             "agenda_days=45\n"
                             "agenda_max_items=2\n"
                             "agenda_max_undated_tasks=8\n"
                             "agenda_refresh_interval=10\n"
                             "agenda_popup_width=900\n"
                             "agenda_show_source=false\n"
                             "agenda_event_color=#8be9fd\n"
                             "tasks=command:evolution --component=tasks\n";
  CHECK(write(fd, AGENDA_TEXT, sizeof(AGENDA_TEXT) - 1) ==
        (ssize_t)(sizeof(AGENDA_TEXT) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&agendaConfig, agendaPath, error, sizeof(error)) == 0);
  CHECK(strcmp(agendaConfig.agendaProvider, "eds") == 0);
  CHECK(agendaConfig.agendaCalendarSourceMode == AGENDA_SOURCES_EXPLICIT);
  CHECK(agendaConfig.agendaCalendarSourceCount == 2);
  CHECK(strcmp(agendaConfig.agendaCalendarSources[0], "personal") == 0);
  CHECK(strcmp(agendaConfig.agendaCalendarSources[1], "work") == 0);
  CHECK(agendaConfig.agendaTaskSourceMode == AGENDA_SOURCES_NONE);
  CHECK(agendaConfig.agendaTaskSourceCount == 0);
  CHECK(agendaConfig.agendaDays == 31);
  CHECK(agendaConfig.agendaMaxItems == 4);
  CHECK(agendaConfig.agendaMaxUndatedTasks == 5);
  CHECK(agendaConfig.agendaRefreshInterval == 60);
  CHECK(agendaConfig.agendaPopupWidth == 720);
  CHECK(!agendaConfig.agendaShowSource);
  CHECK(strcmp(agendaConfig.agendaEventColor, "#8be9fd") == 0);
  CHECK(strcmp(agendaConfig.tasks, "command:evolution --component=tasks") == 0);
  CHECK(unlink(agendaPath) == 0);

  char invalidAgendaPath[] = "/tmp/sliverbar-agenda-invalid-XXXXXX";
  fd = mkstemp(invalidAgendaPath);
  CHECK(fd >= 0);
  const char INVALID_AGENDA_TEXT[] = "agenda_calendar_source=*\n"
                                     "agenda_calendar_source=personal\n";
  CHECK(write(fd, INVALID_AGENDA_TEXT, sizeof(INVALID_AGENDA_TEXT) - 1) ==
        (ssize_t)(sizeof(INVALID_AGENDA_TEXT) - 1));
  CHECK(close(fd) == 0);
  configDefaults(&agendaConfig);
  CHECK(configLoad(&agendaConfig, invalidAgendaPath, error, sizeof(error)) !=
        0);
  CHECK(unlink(invalidAgendaPath) == 0);

  char invalidAgendaValuePath[] = "/tmp/sliverbar-agenda-invalid-value-XXXXXX";
  fd = mkstemp(invalidAgendaValuePath);
  CHECK(fd >= 0);
  const char INVALID_AGENDA_VALUE_TEXT[] = "agenda_show_source=yes\n"
                                           "agenda_event_color=cyan\n";
  CHECK(write(fd,
              INVALID_AGENDA_VALUE_TEXT,
              sizeof(INVALID_AGENDA_VALUE_TEXT) - 1) ==
        (ssize_t)(sizeof(INVALID_AGENDA_VALUE_TEXT) - 1));
  CHECK(close(fd) == 0);
  configDefaults(&agendaConfig);
  CHECK(configLoad(
            &agendaConfig, invalidAgendaValuePath, error, sizeof(error)) != 0);
  CHECK(unlink(invalidAgendaValuePath) == 0);

  CHECK(appSpecAvailable(&cfg, "command:/bin/true"));
  CHECK(!appSpecAvailable(&cfg, "command:/definitely/missing/sliverbar"));
  CHECK(!appSpecAvailable(&cfg, "command:'unterminated"));
  if (appLauncherHasGio()) {
    CHECK(appSearchRank("Überwachung", "Überwachung System", "ÜBER") == 0);
    CHECK(appSearchRank("System Monitor", "System Monitor", "monitor") == 1);
    CHECK(appSearchRank("MySystemTool", "MySystemTool", "system") == 2);
    CHECK(appSearchRank("Network Settings",
                        "Network Settings control center",
                        "network center") == 2);
    CHECK(appSearchRank("Terminal", "Terminal emulator", "browser") < 0);
  }
  CHECK(appSearchRank("Terminal", "Terminal emulator", "") == 3);
  snprintf(cfg.terminal, sizeof(cfg.terminal), "/bin/true");
  CHECK(appTerminalAvailable(&cfg));
  char *terminalProbe[] = {"/bin/true", NULL};
  CHECK(appLaunchTerminal(&cfg, terminalProbe) == 0);
  snprintf(cfg.terminal, sizeof(cfg.terminal), "auto");
  snprintf(cfg.tasks, sizeof(cfg.tasks), "command:/bin/true");
  CHECK(appRoleAvailable(&cfg, APP_ROLE_TASKS));
  CHECK(appLaunchRole(&cfg, APP_ROLE_TASKS) == 0);
  snprintf(
      cfg.tasks, sizeof(cfg.tasks), "command:/definitely/missing/sliverbar");
  CHECK(!appRoleAvailable(&cfg, APP_ROLE_TASKS));
  CHECK(appLaunchRole(&cfg, APP_ROLE_TASKS) != 0);
  snprintf(cfg.tasks, sizeof(cfg.tasks), "auto");

  if (appLauncherHasGio()) {
    char dataHome[] = "/tmp/sliverbar-app-info-XXXXXX";
    CHECK(mkdtemp(dataHome) != NULL);
    char applications[128];
    snprintf(applications, sizeof(applications), "%s/applications", dataHome);
    CHECK(mkdir(applications, 0700) == 0);
    char desktopPath[256];
    snprintf(desktopPath,
             sizeof(desktopPath),
             "%s/sliverbar-test.desktop",
             applications);
    FILE *desktop = fopen(desktopPath, "w");
    CHECK(desktop != NULL);
    CHECK(fputs("[Desktop Entry]\nType=Application\nName=Sliverbar Test\n"
                "Exec=/bin/true\n",
                desktop) >= 0);
    CHECK(fclose(desktop) == 0);
    char hiddenPath[256], invalidPath[256];
    snprintf(hiddenPath,
             sizeof(hiddenPath),
             "%s/sliverbar-hidden.desktop",
             applications);
    desktop = fopen(hiddenPath, "w");
    CHECK(desktop != NULL);
    CHECK(fputs("[Desktop Entry]\nType=Application\nName=Hidden App\n"
                "Exec=/bin/true\nNoDisplay=true\n",
                desktop) >= 0);
    CHECK(fclose(desktop) == 0);
    snprintf(invalidPath,
             sizeof(invalidPath),
             "%s/sliverbar-invalid.desktop",
             applications);
    desktop = fopen(invalidPath, "w");
    CHECK(desktop != NULL);
    CHECK(fputs("this is not a desktop entry\n", desktop) >= 0);
    CHECK(fclose(desktop) == 0);
    const char *oldDataHome = getenv("XDG_DATA_HOME");
    const char *oldDataDirs = getenv("XDG_DATA_DIRS");
    char oldDataHomeCopy[PANEL_PATH_MAX] = "";
    char oldDataDirsCopy[PANEL_PATH_MAX] = "";
    if (oldDataHome)
      snprintf(oldDataHomeCopy, sizeof(oldDataHomeCopy), "%s", oldDataHome);
    if (oldDataDirs)
      snprintf(oldDataDirsCopy, sizeof(oldDataDirsCopy), "%s", oldDataDirs);
    CHECK(setenv("XDG_DATA_HOME", dataHome, 1) == 0);
    CHECK(setenv("XDG_DATA_DIRS", dataHome, 1) == 0);
    CHECK(appSpecAvailable(&cfg, "desktop:sliverbar-test.desktop"));
    CHECK(appLaunchSpec(&cfg, "desktop:sliverbar-test.desktop") == 0);
    AppEntry catalog[16];
    size_t catalogCount = appCatalogLoad(catalog, 16);
    bool catalogContainsTest = false;
    bool catalogContainsHidden = false;
    for (size_t i = 0; i < catalogCount; i++)
      if (strcmp(catalog[i].desktopId, "sliverbar-test.desktop") == 0) {
        catalogContainsTest = true;
        CHECK(strstr(catalog[i].search, "Sliverbar Test") != NULL);
        CHECK(strstr(catalog[i].search, "/bin/true") != NULL);
      } else if (strcmp(catalog[i].desktopId, "sliverbar-hidden.desktop") ==
                 0) {
        catalogContainsHidden = true;
      }
    CHECK(catalogContainsTest);
    CHECK(!catalogContainsHidden);
    if (oldDataHome)
      CHECK(setenv("XDG_DATA_HOME", oldDataHomeCopy, 1) == 0);
    else
      CHECK(unsetenv("XDG_DATA_HOME") == 0);
    if (oldDataDirs)
      CHECK(setenv("XDG_DATA_DIRS", oldDataDirsCopy, 1) == 0);
    else
      CHECK(unsetenv("XDG_DATA_DIRS") == 0);
    CHECK(unlink(desktopPath) == 0);
    CHECK(unlink(hiddenPath) == 0);
    CHECK(unlink(invalidPath) == 0);
    CHECK(rmdir(applications) == 0);
    CHECK(rmdir(dataHome) == 0);
  }

  char quoted[32];
  shellQuoteAction("a:b|c%\n", quoted, sizeof(quoted));
  CHECK(strcmp(quoted, "a_b_c__") == 0);

  char signalPath[] = "/tmp/sliverbar-signals-XXXXXX";
  fd = mkstemp(signalPath);
  CHECK(fd >= 0);
  CHECK(close(fd) == 0);
  CHECK(unlink(signalPath) == 0);

  char inhibitorDirectory[] = "/tmp/sliverbar-inhibitor-XXXXXX";
  CHECK(mkdtemp(inhibitorDirectory) != NULL);
  char inhibitorBackend[256];
  snprintf(inhibitorBackend,
           sizeof(inhibitorBackend),
           "%s/systemd-inhibit",
           inhibitorDirectory);
  FILE *backend = fopen(inhibitorBackend, "w");
  CHECK(backend != NULL);
  CHECK(fputs("#!/bin/sh\nexec \"$5\" \"$6\"\n", backend) >= 0);
  CHECK(fclose(backend) == 0);
  CHECK(chmod(inhibitorBackend, 0700) == 0);
  const char *oldPath = getenv("PATH");
  char oldPathCopy[PANEL_PATH_MAX] = "";
  if (oldPath)
    snprintf(oldPathCopy, sizeof(oldPathCopy), "%s", oldPath);
  CHECK(setenv("PATH", inhibitorDirectory, 1) == 0);
  Inhibitor *inhibitor = inhibitorCreate(argv[0]);
  CHECK(inhibitorAvailable(inhibitor));
  CHECK(strcmp(inhibitorBackendName(inhibitor), "systemd-inhibit") == 0);
  CHECK(!inhibitorActive(inhibitor));
  CHECK(inhibitorToggle(inhibitor) == 0);
  pid_t inhibitorChild = inhibitorPid(inhibitor);
  CHECK(inhibitorChild > 0);
  for (int i = 0; i < 20 && !inhibitorActive(inhibitor); i++)
    usleep(10000);
  CHECK(inhibitorActive(inhibitor));
  CHECK(kill(inhibitorChild, SIGKILL) == 0);
  CHECK(waitpid(inhibitorChild, NULL, 0) == inhibitorChild);
  CHECK(inhibitorChildExited(inhibitor, inhibitorChild));
  CHECK(!inhibitorActive(inhibitor));
  CHECK(inhibitorSetActive(inhibitor, true) == 0);
  CHECK(inhibitorActive(inhibitor));
  CHECK(inhibitorSetActive(inhibitor, true) == 0);
  CHECK(inhibitorSetActive(inhibitor, false) == 0);
  CHECK(!inhibitorActive(inhibitor));
  inhibitorDestroy(inhibitor);
  if (oldPath)
    CHECK(setenv("PATH", oldPathCopy, 1) == 0);
  else
    CHECK(unsetenv("PATH") == 0);
  CHECK(unlink(inhibitorBackend) == 0);
  CHECK(rmdir(inhibitorDirectory) == 0);
  sigset_t blocked, oldMask;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGCHLD);
  CHECK(sigprocmask(SIG_BLOCK, &blocked, &oldMask) == 0);
  struct sigaction ignored = {.sa_handler = SIG_IGN}, oldPipe;
  sigemptyset(&ignored.sa_mask);
  CHECK(sigaction(SIGPIPE, &ignored, &oldPipe) == 0);
  char *probeArgv[] = {argv[0], "--signal-probe", signalPath, NULL};
  CHECK(spawnDetached(probeArgv) == 0);
  CHECK(sigprocmask(SIG_SETMASK, &oldMask, NULL) == 0);
  CHECK(sigaction(SIGPIPE, &oldPipe, NULL) == 0);
  for (int i = 0; i < 100 && access(signalPath, F_OK) != 0; i++)
    usleep(10000);
  char signalResult[16];
  CHECK(readTextFile(signalPath, signalResult, sizeof(signalResult)) == 0);
  CHECK(strcmp(signalResult, "ok") == 0);
  CHECK(unlink(signalPath) == 0);
  CHECK(sigprocmask(SIG_BLOCK, &blocked, &oldMask) == 0);
  CHECK(sigaction(SIGPIPE, &ignored, &oldPipe) == 0);
  pid_t trackedProbe = spawnTracked(probeArgv);
  CHECK(trackedProbe > 0);
  int trackedStatus = 0;
  CHECK(waitpid(trackedProbe, &trackedStatus, 0) == trackedProbe);
  CHECK(WIFEXITED(trackedStatus) && WEXITSTATUS(trackedStatus) == 0);
  CHECK(sigprocmask(SIG_SETMASK, &oldMask, NULL) == 0);
  CHECK(sigaction(SIGPIPE, &oldPipe, NULL) == 0);
  CHECK(readTextFile(signalPath, signalResult, sizeof(signalResult)) == 0);
  CHECK(strcmp(signalResult, "ok") == 0);
  CHECK(unlink(signalPath) == 0);
  char *missingArgv[] = {"/sliverbar-test-command-does-not-exist", NULL};
  pid_t missingProbe = spawnTracked(missingArgv);
  CHECK(missingProbe > 0);
  CHECK(waitpid(missingProbe, &trackedStatus, 0) == missingProbe);
  CHECK(WIFEXITED(trackedStatus) && WEXITSTATUS(trackedStatus) == 127);
  const char *currentNumericLocale = setlocale(LC_NUMERIC, NULL);
  CHECK(currentNumericLocale != NULL);
  char numericLocale[128];
  snprintf(numericLocale, sizeof(numericLocale), "%s", currentNumericLocale);
  const char *germanNumericLocale = setlocale(LC_NUMERIC, "de_DE.UTF-8");
  if (germanNumericLocale != NULL) {
    CHECK(strcmp(localeconv()->decimal_point, ",") == 0);
  } else {
    CHECK(setlocale(LC_NUMERIC, "C") != NULL);
  }
  char brightnessFactor[16];
  int brightnessPercent = 0;
  CHECK(brightnessFactorFormat(99, brightnessFactor, sizeof(brightnessFactor)));
  CHECK(strcmp(brightnessFactor, "0.99") == 0);
  CHECK(brightnessFactorFormat(5, brightnessFactor, sizeof(brightnessFactor)));
  CHECK(strcmp(brightnessFactor, "0.05") == 0);
  CHECK(
      brightnessFactorFormat(100, brightnessFactor, sizeof(brightnessFactor)));
  CHECK(strcmp(brightnessFactor, "1.00") == 0);
  CHECK(
      !brightnessFactorFormat(-1, brightnessFactor, sizeof(brightnessFactor)));
  CHECK(brightnessFactorParse("0.99\n", &brightnessPercent));
  CHECK(brightnessPercent == 99);
  CHECK(brightnessFactorParse(" 1.0\n", &brightnessPercent));
  CHECK(brightnessPercent == 100);
  CHECK(brightnessFactorParse("0.995\n", &brightnessPercent));
  CHECK(brightnessPercent == 100);
  CHECK(!brightnessFactorParse("0,99\n", &brightnessPercent));
  CHECK(!brightnessFactorParse("invalid", &brightnessPercent));
  CHECK(brightnessPercentFromRaw(48000, 48000) == 100);
  CHECK(brightnessPercentFromRaw(24000, 48000) == 50);
  CHECK(brightnessPercentFromRaw(1, 3) == 33);
  CHECK(brightnessPercentFromRaw(4, 3) == 100);
  CHECK(brightnessPercentFromRaw(1, 0) == 0);
  CHECK(brightnessRawFromPercent(100, 48000) == 48000);
  CHECK(brightnessRawFromPercent(50, 48000) == 24000);
  CHECK(brightnessRawFromPercent(33, 3) == 1);
  CHECK(brightnessRawFromPercent(101, 48000) == 48000);
  CHECK(brightnessRawFromPercent(-1, 48000) == 0);
  CHECK(setlocale(LC_NUMERIC, numericLocale) != NULL);
  PanelState brightnessState = {0};
  moduleBrightnessValue(&cfg, &brightnessState, 42);
  CHECK(strstr(brightnessState.brightness, "%{O4}42%") != NULL);
  CHECK(strstr(brightnessState.brightness, "%{O9}") != NULL);
  brightnessState.brightnessInitialized = true;
  strcpy(brightnessState.brightnessOutput, "DP-3");
  brightnessState.brightnessPercent = 42;
  cfg.brightnessStep = 1;
  CHECK(moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  CHECK(moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  CHECK(moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  CHECK(brightnessState.brightnessPercent == 39);
  CHECK(strstr(brightnessState.brightness, "%{O4}39%") != NULL);
  brightnessState.brightnessUpdatePending = true;
  CHECK(!moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  CHECK(brightnessState.brightnessPercent == 39);
  brightnessState.brightnessUpdatePending = false;
  CHECK(!moduleBrightnessAdjust(&cfg, &brightnessState, "invalid"));
  brightnessState.brightnessPercent = 5;
  CHECK(!moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  brightnessState.brightnessPercent = 100;
  CHECK(!moduleBrightnessAdjust(&cfg, &brightnessState, "up"));
  moduleInhibitor(&cfg, &brightnessState, false, false);
  CHECK(brightnessState.inhibitor[0] == '\0');
  moduleInhibitor(&cfg, &brightnessState, true, false);
  CHECK(strstr(brightnessState.inhibitor, cfg.colorFree) != NULL);
  moduleInhibitor(&cfg, &brightnessState, true, true);
  CHECK(strstr(brightnessState.inhibitor, cfg.colorWarning) != NULL);
  cfg.iconFont[0] = '\0';
  CHECK(strcmp(moduleClockGlyph(&cfg, 0), "◷") == 0);
  snprintf(cfg.iconFont, sizeof(cfg.iconFont), "Test Nerd Font");
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
  for (unsigned hour = 1; hour <= 12; hour++)
    CHECK(strcmp(moduleClockGlyph(&cfg, hour), CLOCK_GLYPHS[hour - 1]) == 0);
  CHECK(strcmp(moduleClockGlyph(&cfg, 0), CLOCK_GLYPHS[11]) == 0);
  CHECK(strcmp(moduleClockGlyph(&cfg, 13), CLOCK_GLYPHS[0]) == 0);
  PanelState timerPanelState = {0};
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_EMPTY, 0);
  CHECK(strstr(timerPanelState.timer, cfg.colorClock) != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀠") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|toggle") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|reset") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|up") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|down") != NULL);
  moduleTimer(&cfg, &timerPanelState, 12, TIMER_DISPLAY_SET, 0);
  CHECK(strstr(timerPanelState.timer, cfg.colorUrgent) != NULL);
  CHECK(strstr(timerPanelState.timer, "12%{O4}") != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀡") != NULL);
  CHECK(strstr(timerPanelState.timer, "%{O9}") != NULL);
  static const char *const TIMER_ANIMATION_GLYPHS[] = {
      "󰪞", "󰪟", "󰪠", "󰪡", "󰪢", "󰪣", "󰪤", "󰪥"};
  for (unsigned frame = 0; frame < TIMER_ANIMATION_FRAMES; frame++) {
    moduleTimer(&cfg, &timerPanelState, 11, TIMER_DISPLAY_RUNNING, frame);
    CHECK(strstr(timerPanelState.timer, cfg.colorUrgent) != NULL);
    CHECK(strstr(timerPanelState.timer, TIMER_ANIMATION_GLYPHS[frame]) != NULL);
  }
  moduleTimer(&cfg, &timerPanelState, 10, TIMER_DISPLAY_PAUSED, 0);
  CHECK(strstr(timerPanelState.timer, cfg.colorUrgent) != NULL);
  CHECK(strstr(timerPanelState.timer, "󰚎") != NULL);
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_EXPIRED, 0);
  CHECK(strstr(timerPanelState.timer, cfg.colorClock) != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀢") != NULL);
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_RESET, 0);
  CHECK(strstr(timerPanelState.timer, cfg.colorClock) != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀣") != NULL);
  cfg.iconFont[0] = '\0';
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_RUNNING, 4);
  CHECK(strstr(timerPanelState.timer, "⏲") != NULL);

  Timer timer = {0};
  CHECK(timerMinutes(&timer) == 0);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_EMPTY);
  CHECK(!timerResetWithFeedback(&timer));
  CHECK(!timerAdjust(&timer, -1));
  CHECK(timerAdjust(&timer, 1));
  CHECK(timerAdjust(&timer, 1));
  CHECK(timer.status == TIMER_SET);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_SET);
  CHECK(timerMinutes(&timer) == 2);
  CHECK(timerToggle(&timer, UINT64_C(1000000000)) == TIMER_TRANSITION_STARTED);
  CHECK(timer.status == TIMER_RUNNING);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_RUNNING);
  CHECK(timerAnimationFrame(&timer, UINT64_C(1000000000)) == 0);
  CHECK(timerAnimationFrame(&timer, UINT64_C(1124999999)) == 0);
  CHECK(timerAnimationFrame(&timer, UINT64_C(1125000000)) == 1);
  CHECK(timerAnimationFrame(&timer, UINT64_C(1999999999)) == 7);
  CHECK(timerAnimationFrame(&timer, UINT64_C(2000000000)) == 0);
  CHECK(!timerAdjust(&timer, 1));
  CHECK(!timerUpdate(&timer, UINT64_C(62000000000)));
  CHECK(timerMinutes(&timer) == 1);
  CHECK(timerToggle(&timer, UINT64_C(62000000000)) == TIMER_TRANSITION_PAUSED);
  CHECK(timer.status == TIMER_PAUSED);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_PAUSED);
  CHECK(timerAnimationFrame(&timer, UINT64_C(62000000000)) == 0);
  CHECK(!timerUpdate(&timer, UINT64_C(999000000000)));
  CHECK(timerMinutes(&timer) == 1);
  CHECK(timerToggle(&timer, UINT64_C(999000000000)) ==
        TIMER_TRANSITION_RESUMED);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_RUNNING);
  CHECK(timerAnimationFrame(&timer, UINT64_C(999000000000)) == 0);
  CHECK(timerAnimationFrame(&timer, UINT64_C(999125000000)) == 1);
  CHECK(timerUpdate(&timer, UINT64_C(1058000000000)));
  CHECK(timer.status == TIMER_EMPTY);
  CHECK(timerMinutes(&timer) == 0);
  timerShowExpired(&timer);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_EXPIRED);
  CHECK(timerSoundFinished(&timer, false) == TIMER_FEEDBACK_TIMEOUT);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_EXPIRED);
  CHECK(timerSoundFinished(&timer, true) == TIMER_FEEDBACK_CANCEL);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_EMPTY);
  CHECK(timerSoundFinished(&timer, true) == TIMER_FEEDBACK_NONE);
  int feedbackTimer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  CHECK(feedbackTimer >= 0);
  CHECK(timerFeedbackTimeoutSet(feedbackTimer, true) == 0);
  struct itimerspec feedbackTimeout = {0};
  CHECK(timerfd_gettime(feedbackTimer, &feedbackTimeout) == 0);
  CHECK(feedbackTimeout.it_value.tv_sec == 1);
  CHECK(feedbackTimeout.it_value.tv_nsec > 0);
  CHECK(timerFeedbackTimeoutSet(feedbackTimer, false) == 0);
  CHECK(timerfd_gettime(feedbackTimer, &feedbackTimeout) == 0);
  CHECK(feedbackTimeout.it_value.tv_sec == 0);
  CHECK(feedbackTimeout.it_value.tv_nsec == 0);
  CHECK(timerAnimationTimeoutSet(feedbackTimer, true) == 0);
  CHECK(timerfd_gettime(feedbackTimer, &feedbackTimeout) == 0);
  CHECK(feedbackTimeout.it_value.tv_sec == 0);
  CHECK(feedbackTimeout.it_value.tv_nsec > 0);
  CHECK(feedbackTimeout.it_interval.tv_sec == 0);
  CHECK(feedbackTimeout.it_interval.tv_nsec == 125000000);
  CHECK(timerAnimationTimeoutSet(feedbackTimer, false) == 0);
  CHECK(timerfd_gettime(feedbackTimer, &feedbackTimeout) == 0);
  CHECK(feedbackTimeout.it_value.tv_sec == 0);
  CHECK(feedbackTimeout.it_value.tv_nsec == 0);
  CHECK(feedbackTimeout.it_interval.tv_nsec == 0);
  CHECK(close(feedbackTimer) == 0);
  CHECK(timerAdjust(&timer, 1));
  CHECK(timerResetWithFeedback(&timer));
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_RESET);
  CHECK(timerAdjust(&timer, 1));
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_SET);
  timerReset(&timer);
  CHECK(timer.status == TIMER_EMPTY);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_EMPTY);
  for (unsigned i = 0; i < TIMER_MAX_MINUTES; i++)
    CHECK(timerAdjust(&timer, 1));
  CHECK(timerMinutes(&timer) == TIMER_MAX_MINUTES);
  CHECK(!timerAdjust(&timer, 1));
  timerReset(&timer);
  CHECK(strcmp(moduleBatteryStatusGlyph("Charging"), "") == 0);
  CHECK(strcmp(moduleBatteryStatusGlyph("Full"), "") == 0);
  CHECK(strcmp(moduleBatteryStatusGlyph("Discharging"), "") == 0);
  CHECK(strcmp(moduleBatteryStatusGlyph("Not charging"), "") == 0);
  CHECK(strcmp(moduleBatteryStatusGlyph("Unknown"), "") == 0);
  CHECK(strcmp(moduleBatteryStatusGlyph(NULL), "") == 0);
  PanelState batteryState = {0};
  moduleBattery(&cfg, &batteryState);
  if (access("/sys/class/power_supply", R_OK) == 0) {
    CHECK(batteryState.battery[0] != '\0');
    CHECK(strstr(batteryState.battery, "%{O4}") != NULL);
  }
  cfg.internalPowerProfilesAvailable = true;
  moduleBattery(&cfg, &batteryState);
  CHECK(strstr(batteryState.battery, "%{A1:power_profile|menu:}") != NULL);
  cfg.internalPowerProfilesAvailable = false;
  PowerProfileState profileState = {
      .available = true,
      .count = 2,
      .profiles = {{.id = "power-saver"}, {.id = "balanced"}},
  };
  CHECK(powerProfileOffered(&profileState, "power-saver"));
  CHECK(powerProfileOffered(&profileState, "balanced"));
  CHECK(!powerProfileOffered(&profileState, "performance"));
  CHECK(powerProfileIdValid("power-saver"));
  CHECK(!powerProfileIdValid("power-saver|poweroff"));
  CHECK(!powerProfileIdValid("Performance"));
  CHECK(strcmp(powerProfilesBackendName(&profileState),
               "power-profiles-daemon-dbus") == 0);
  strcpy(cfg.language, "de");
  CHECK(strcmp(powerProfileLabel(&cfg, "balanced"), "Ausgeglichen") == 0);
  strcpy(cfg.language, "en");
  CHECK(strcmp(powerProfileLabel(&cfg, "power-saver"), "Power saver") == 0);
  FakePowerProfiles fakeProfiles = {
      .state =
          {
              .available = true,
              .active = "balanced",
              .profiles = {{.id = "power-saver"}, {.id = "balanced"}},
              .count = 2,
          },
  };
  PowerProfilesBackend fakeBackend = {
      .query = fakePowerProfilesQuery,
      .set = fakePowerProfileSet,
      .context = &fakeProfiles,
  };
  powerProfilesSetBackendForTests(&fakeBackend);
  PowerProfileState queriedProfiles;
  CHECK(powerProfilesQuery(&cfg, &queriedProfiles) == 0);
  CHECK(strcmp(queriedProfiles.active, "balanced") == 0);
  char profileError[64];
  CHECK(powerProfileSet("power-saver", profileError, sizeof(profileError)) ==
        0);
  CHECK(fakeProfiles.setCalls == 1);
  CHECK(strcmp(fakeProfiles.state.active, "power-saver") == 0);
  fakeProfiles.reject = true;
  CHECK(powerProfileSet("balanced", profileError, sizeof(profileError)) != 0);
  CHECK(strcmp(profileError, "authorization rejected") == 0);
  fakeProfiles.state.available = false;
  CHECK(powerProfilesQuery(&cfg, &queriedProfiles) != 0);
  powerProfilesSetBackendForTests(NULL);

  PanelState state = {0};
  strcpy(state.launcher, "L");
  strcpy(state.workspace, "W");
  strcpy(state.title, "T");
  strcpy(state.timer, "M");
  strcpy(state.inhibitor, "I");
  strcpy(state.weather, "R");
  strcpy(state.battery, "B");
  strcpy(state.clock, "C");
  char rendered[128];
  renderPanel(&state, rendered, sizeof(rendered));
  CHECK(strcmp(rendered, "%{l}LW%{c}T%{r}MIRBC\n") == 0);

  char ssid[128];
  int strength;
  CHECK(parseNmcliWifi(
            " :guest:20\n*:Home WiFi:73\n", ssid, sizeof(ssid), &strength) ==
        0);
  CHECK(strcmp(ssid, "Home WiFi") == 0);
  CHECK(strength == 73);
  CHECK(parseNmcliWifi(
            "yes:SSID:with:colons:88\n", ssid, sizeof(ssid), &strength) == 0);
  CHECK(strcmp(ssid, "SSID:with:colons") == 0);
  CHECK(strength == 88);
  CHECK(parseNmcliWifi("*:broken:unknown\n", ssid, sizeof(ssid), &strength) !=
        0);
  CHECK(parseNmcliWifi(" :shared:20\n*:shared:81\n :shared:90\n",
                       ssid,
                       sizeof(ssid),
                       &strength) == 0);
  CHECK(strength == 81);
  CHECK(wifiQualityPercent(51.0) == 73);
  CHECK(wifiQualityPercent(34.5) == 49);
  CHECK(wifiQualityPercent(0.0) == 0);
  CHECK(wifiQualityPercent(70.0) == 100);
  CHECK(wifiQualityPercent(90.0) == 100);
  CHECK(wifiQualityPercent(-1.0) == -1);
  const char WIRELESS[] =
      "Inter-| sta-|   Quality        |   Discarded packets\n"
      " face | tus | link level noise |  nwid  crypt   frag\n"
      " wlan0: 0000   12.  -80.  -256        0      0      0\n"
      " wlo1: 0000   51.  -59.  -256        0      0      0\n";
  double rawQuality = 0.0;
  CHECK(parseWirelessQuality(WIRELESS, "wlo1", &rawQuality, &strength) == 0);
  CHECK(rawQuality == 51.0);
  CHECK(strength == 73);
  CHECK(parseWirelessQuality(WIRELESS, "missing", &rawQuality, &strength) != 0);
  CHECK(parseWirelessQuality(
            " wlo1: 0000 nan -59. -256\n", "wlo1", &rawQuality, &strength) !=
        0);

  const char ROUTES[] =
      "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\n"
      "eth0\t0000A8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\n"
      "wlo1\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\n";
  char routeInterface[32];
  CHECK(parseDefaultRouteInterface(
            ROUTES, routeInterface, sizeof(routeInterface)) == 0);
  CHECK(strcmp(routeInterface, "wlo1") == 0);
  CHECK(parseDefaultRouteInterface("Iface\tDestination\n",
                                   routeInterface,
                                   sizeof(routeInterface)) != 0);

  WorkspaceSnapshot snapshot = {.count = 3, .current = 1};
  snapshot.occupied[0] = true;
  snapshot.occupied[1] = true;
  snapshot.urgent[2] = true;
  strcpy(snapshot.names[0], "web");
  strcpy(snapshot.names[1], "code");
  strcpy(snapshot.names[2], "chat");
  moduleWorkspaceEwmh(&cfg, &state, &snapshot);
  CHECK(state.focusedWorkspaceKnown);
  CHECK(state.focusedWorkspaceOccupied);
  CHECK(strstr(state.workspace, "workspace|0") != NULL);
  CHECK(strstr(state.workspace, "workspace|1") != NULL);
  CHECK(strstr(state.workspace, cfg.colorFocusedOccupied) != NULL);
  CHECK(strstr(state.workspace, cfg.colorUrgent) != NULL);

  snapshot.occupied[1] = false;
  moduleWorkspaceEwmh(&cfg, &state, &snapshot);
  CHECK(state.focusedWorkspaceKnown);
  CHECK(!state.focusedWorkspaceOccupied);

  moduleWorkspace(&cfg, &state, "WMDP-3:O1:o2:f3:LT:TT:G");
  CHECK(state.focusedWorkspaceKnown);
  CHECK(state.focusedWorkspaceOccupied);
  CHECK(strstr(state.workspace, "%{F#69FF94}%{B#191A21}") != NULL);
  CHECK(strstr(state.workspace, "%{F#ff5555}%{B#191A21}") != NULL);
  CHECK(strstr(state.workspace, "[TILED]") != NULL);

  moduleWorkspace(&cfg, &state, "WMDP-3:U1:u2:f3:LT:TT:G");
  CHECK(state.focusedWorkspaceKnown);
  CHECK(state.focusedWorkspaceOccupied);
  CHECK(strstr(state.workspace, "%{F#69FF94}%{B#191A21}") != NULL);
  CHECK(strstr(state.workspace, "%{F#FF6E6E}%{B#343746}") != NULL);
  CHECK(strstr(state.workspace, "%{F#343746}%{B#FF6E6E}") == NULL);

  moduleWorkspace(&cfg, &state, "WMDP-3:F1:o2:f3:LT:TT:G");
  CHECK(state.focusedWorkspaceKnown);
  CHECK(!state.focusedWorkspaceOccupied);

  PanelConfig weatherConfig;
  configDefaults(&weatherConfig);
  char weatherPath[] = "/tmp/sliverbar-weather-XXXXXX";
  fd = mkstemp(weatherPath);
  CHECK(fd >= 0);
  const char WEATHER_CONFIG[] =
      "weather_location=munich|München|Munich\n"
      "weather_location=berlin|Berlin|Berlin, Germany\n"
      "weather_default=berlin\n";
  CHECK(write(fd, WEATHER_CONFIG, sizeof(WEATHER_CONFIG) - 1) ==
        (ssize_t)(sizeof(WEATHER_CONFIG) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&weatherConfig, weatherPath, error, sizeof(error)) == 0);
  CHECK(weatherConfig.weatherLocationCount == 2);
  CHECK(weatherConfig.activeWeatherLocation == 1);
  CHECK(strcmp(weatherConfig.location, "Berlin, Germany") == 0);
  CHECK(strcmp(weatherConfig.weatherLocations[0].label, "München") == 0);
  CHECK(unlink(weatherPath) == 0);

  PanelConfig deprecatedWeatherImageConfig;
  configDefaults(&deprecatedWeatherImageConfig);
  char deprecatedWeatherImagePath[] = "/tmp/sliverbar-weather-image-XXXXXX";
  fd = mkstemp(deprecatedWeatherImagePath);
  CHECK(fd >= 0);
  const char DEPRECATED_WEATHER_IMAGE[] =
      "location=Munich\nweather_image=/tmp/forecast.png\n";
  CHECK(write(fd,
              DEPRECATED_WEATHER_IMAGE,
              sizeof(DEPRECATED_WEATHER_IMAGE) - 1) ==
        (ssize_t)(sizeof(DEPRECATED_WEATHER_IMAGE) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&deprecatedWeatherImageConfig,
                   deprecatedWeatherImagePath,
                   error,
                   sizeof(error)) == 0);
  CHECK(strcmp(deprecatedWeatherImageConfig.weatherImage,
               "/tmp/forecast.png") == 0);
  CHECK(unlink(deprecatedWeatherImagePath) == 0);

  PanelConfig boundedWeatherConfig;
  configDefaults(&boundedWeatherConfig);
  char boundedWeatherPath[] = "/tmp/sliverbar-weather-bounds-XXXXXX";
  fd = mkstemp(boundedWeatherPath);
  CHECK(fd >= 0);
  const char BOUNDED_WEATHER[] = "weather_location=one|One|One\n"
                                 "weather_location=two|Two|Two\n"
                                 "weather_location=three|Three|Three\n"
                                 "weather_location=four|Four|Four\n"
                                 "weather_interval=900\n";
  CHECK(write(fd, BOUNDED_WEATHER, sizeof(BOUNDED_WEATHER) - 1) ==
        (ssize_t)(sizeof(BOUNDED_WEATHER) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(
            &boundedWeatherConfig, boundedWeatherPath, error, sizeof(error)) ==
        0);
  CHECK(boundedWeatherConfig.weatherLocationCount ==
        PANEL_WEATHER_LOCATION_MAX);
  CHECK(boundedWeatherConfig.weatherInterval == PANEL_WEATHER_INTERVAL_MIN);
  CHECK(unlink(boundedWeatherPath) == 0);

  PanelConfig maximumWeatherIntervalConfig;
  configDefaults(&maximumWeatherIntervalConfig);
  char maximumWeatherIntervalPath[] = "/tmp/sliverbar-weather-maximum-XXXXXX";
  fd = mkstemp(maximumWeatherIntervalPath);
  CHECK(fd >= 0);
  const char MAXIMUM_WEATHER_INTERVAL[] = "weather_interval=42000\n";
  CHECK(write(fd,
              MAXIMUM_WEATHER_INTERVAL,
              sizeof(MAXIMUM_WEATHER_INTERVAL) - 1) ==
        (ssize_t)(sizeof(MAXIMUM_WEATHER_INTERVAL) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&maximumWeatherIntervalConfig,
                   maximumWeatherIntervalPath,
                   error,
                   sizeof(error)) == 0);
  CHECK(maximumWeatherIntervalConfig.weatherInterval ==
        PANEL_WEATHER_INTERVAL_MAX);
  CHECK(unlink(maximumWeatherIntervalPath) == 0);

  PanelConfig tooManyWeatherLocationsConfig;
  configDefaults(&tooManyWeatherLocationsConfig);
  char tooManyWeatherLocationsPath[] = "/tmp/sliverbar-weather-too-many-XXXXXX";
  fd = mkstemp(tooManyWeatherLocationsPath);
  CHECK(fd >= 0);
  const char TOO_MANY_WEATHER_LOCATIONS[] =
      "weather_location=one|One|One\n"
      "weather_location=two|Two|Two\n"
      "weather_location=three|Three|Three\n"
      "weather_location=four|Four|Four\n"
      "weather_location=five|Five|Five\n";
  CHECK(write(fd,
              TOO_MANY_WEATHER_LOCATIONS,
              sizeof(TOO_MANY_WEATHER_LOCATIONS) - 1) ==
        (ssize_t)(sizeof(TOO_MANY_WEATHER_LOCATIONS) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&tooManyWeatherLocationsConfig,
                   tooManyWeatherLocationsPath,
                   error,
                   sizeof(error)) != 0);
  CHECK(unlink(tooManyWeatherLocationsPath) == 0);

  PanelConfig duplicateWeatherConfig;
  configDefaults(&duplicateWeatherConfig);
  char duplicateWeatherPath[] = "/tmp/sliverbar-weather-duplicate-XXXXXX";
  fd = mkstemp(duplicateWeatherPath);
  CHECK(fd >= 0);
  const char DUPLICATE_WEATHER[] = "weather_location=home|Home|Munich\n"
                                   "weather_location=home|Duplicate|Berlin\n";
  CHECK(write(fd, DUPLICATE_WEATHER, sizeof(DUPLICATE_WEATHER) - 1) ==
        (ssize_t)(sizeof(DUPLICATE_WEATHER) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&duplicateWeatherConfig,
                   duplicateWeatherPath,
                   error,
                   sizeof(error)) != 0);
  CHECK(unlink(duplicateWeatherPath) == 0);

  char invalidPath[] = "/tmp/sliverbar-invalid-XXXXXX";
  fd = mkstemp(invalidPath);
  CHECK(fd >= 0);
  const char INVALID[] = "unknown_key=value\n";
  CHECK(write(fd, INVALID, sizeof(INVALID) - 1) ==
        (ssize_t)(sizeof(INVALID) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&cfg, invalidPath, error, sizeof(error)) != 0);
  CHECK(unlink(invalidPath) == 0);

  char invalidBackendPath[] = "/tmp/sliverbar-invalid-backend-XXXXXX";
  fd = mkstemp(invalidBackendPath);
  CHECK(fd >= 0);
  const char INVALID_BACKEND[] = "workspace_backend=invalid\n";
  CHECK(write(fd, INVALID_BACKEND, sizeof(INVALID_BACKEND) - 1) ==
        (ssize_t)(sizeof(INVALID_BACKEND) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&cfg, invalidBackendPath, error, sizeof(error)) != 0);
  CHECK(unlink(invalidBackendPath) == 0);

  char invalidModulePath[] = "/tmp/sliverbar-invalid-module-XXXXXX";
  fd = mkstemp(invalidModulePath);
  CHECK(fd >= 0);
  const char INVALID_MODULE[] = "module_clock=sometimes\n";
  CHECK(write(fd, INVALID_MODULE, sizeof(INVALID_MODULE) - 1) ==
        (ssize_t)(sizeof(INVALID_MODULE) - 1));
  CHECK(close(fd) == 0);
  CHECK(configLoad(&cfg, invalidModulePath, error, sizeof(error)) != 0);
  CHECK(unlink(invalidModulePath) == 0);
  puts("ok");
  return 0;
}
