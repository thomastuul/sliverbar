#include "panel.h"

#include "app_launcher.h"
#include "inhibitor.h"
#include "power_actions.h"
#include "timer.h"

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

  PanelConfig cfg;
  configDefaults(&cfg);
  CHECK(cfg.height == 25);
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
  CHECK(strstr(cfg.timerSound, "alarm-clock-elapsed.oga") != NULL);
  CHECK(cfg.location[0] == '\0');
  CHECK(cfg.moduleClock == MODULE_AUTO);
  CHECK(moduleModeActive(MODULE_AUTO, true));
  CHECK(!moduleModeActive(MODULE_AUTO, false));
  CHECK(moduleModeActive(MODULE_ENABLED, false));
  CHECK(!moduleModeActive(MODULE_DISABLED, true));
  snprintf(cfg.language, sizeof(cfg.language), "de");
  CHECK(panelLanguageIsGerman(&cfg));
  CHECK(strcmp(powerActionLabel(&cfg, "poweroff"), "Ausschalten") == 0);
  snprintf(cfg.language, sizeof(cfg.language), "en");
  CHECK(!panelLanguageIsGerman(&cfg));
  CHECK(strcmp(powerActionLabel(&cfg, "poweroff"), "Power off") == 0);
  snprintf(cfg.language, sizeof(cfg.language), "auto");

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
      "height=31\nvolume_step=4\ncolor_bg=#000000\nmodule_cpu=disabled\n"
      "power_actions=lock,reboot,poweroff\npower_confirm=reboot,poweroff\n"
      "module_timer=enabled\ntimer_sound=/tmp/timer.oga\n";
  CHECK(write(fd, TEXT, sizeof(TEXT) - 1) == (ssize_t)(sizeof(TEXT) - 1));
  CHECK(close(fd) == 0);

  char error[256];
  CHECK(configLoad(&cfg, path, error, sizeof(error)) == 0);
  CHECK(cfg.height == 31);
  CHECK(cfg.volumeStep == 4);
  CHECK(strcmp(cfg.colorBg, "#000000") == 0);
  CHECK(strcmp(cfg.colorOccupied, "#ff5555") == 0);
  CHECK(cfg.moduleCpu == MODULE_DISABLED);
  CHECK(cfg.moduleTimer == MODULE_ENABLED);
  CHECK(strcmp(cfg.timerSound, "/tmp/timer.oga") == 0);
  CHECK(powerActionAllowed(cfg.powerActions, "reboot"));
  CHECK(!powerActionAllowed(cfg.powerActions, "hibernate"));
  CHECK(unlink(path) == 0);

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
  PanelState brightnessState = {0};
  moduleBrightnessValue(&cfg, &brightnessState, 42);
  CHECK(strstr(brightnessState.brightness, "  42%") != NULL);
  brightnessState.brightnessInitialized = true;
  strcpy(brightnessState.brightnessOutput, "DP-3");
  brightnessState.brightnessPercent = 42;
  cfg.brightnessStep = 1;
  CHECK(moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  CHECK(moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  CHECK(moduleBrightnessAdjust(&cfg, &brightnessState, "down"));
  CHECK(brightnessState.brightnessPercent == 39);
  CHECK(strstr(brightnessState.brightness, "  39%") != NULL);
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
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_EMPTY);
  CHECK(strstr(timerPanelState.timer, cfg.colorClock) != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀠") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|toggle") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|reset") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|up") != NULL);
  CHECK(strstr(timerPanelState.timer, "timer|down") != NULL);
  moduleTimer(&cfg, &timerPanelState, 12, TIMER_DISPLAY_ACTIVE);
  CHECK(strstr(timerPanelState.timer, cfg.colorUrgent) != NULL);
  CHECK(strstr(timerPanelState.timer, "12 ") != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀡") != NULL);
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_EXPIRED);
  CHECK(strstr(timerPanelState.timer, cfg.colorClock) != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀢") != NULL);
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_RESET);
  CHECK(strstr(timerPanelState.timer, cfg.colorClock) != NULL);
  CHECK(strstr(timerPanelState.timer, "󰀣") != NULL);
  cfg.iconFont[0] = '\0';
  moduleTimer(&cfg, &timerPanelState, 0, TIMER_DISPLAY_RESET);
  CHECK(strstr(timerPanelState.timer, "⏲") != NULL);

  Timer timer = {0};
  CHECK(timerMinutes(&timer) == 0);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_EMPTY);
  CHECK(!timerResetWithFeedback(&timer));
  CHECK(!timerAdjust(&timer, -1));
  CHECK(timerAdjust(&timer, 1));
  CHECK(timerAdjust(&timer, 1));
  CHECK(timer.status == TIMER_SET);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_ACTIVE);
  CHECK(timerMinutes(&timer) == 2);
  CHECK(timerToggle(&timer, UINT64_C(1000000000)) == TIMER_TRANSITION_STARTED);
  CHECK(timer.status == TIMER_RUNNING);
  CHECK(!timerAdjust(&timer, 1));
  CHECK(!timerUpdate(&timer, UINT64_C(62000000000)));
  CHECK(timerMinutes(&timer) == 1);
  CHECK(timerToggle(&timer, UINT64_C(62000000000)) == TIMER_TRANSITION_PAUSED);
  CHECK(timer.status == TIMER_PAUSED);
  CHECK(!timerUpdate(&timer, UINT64_C(999000000000)));
  CHECK(timerMinutes(&timer) == 1);
  CHECK(timerToggle(&timer, UINT64_C(999000000000)) ==
        TIMER_TRANSITION_RESUMED);
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
  CHECK(close(feedbackTimer) == 0);
  CHECK(timerAdjust(&timer, 1));
  CHECK(timerResetWithFeedback(&timer));
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_RESET);
  CHECK(timerAdjust(&timer, 1));
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_ACTIVE);
  timerReset(&timer);
  CHECK(timer.status == TIMER_EMPTY);
  CHECK(timerDisplay(&timer) == TIMER_DISPLAY_EMPTY);
  for (unsigned i = 0; i < TIMER_MAX_MINUTES; i++)
    CHECK(timerAdjust(&timer, 1));
  CHECK(timerMinutes(&timer) == TIMER_MAX_MINUTES);
  CHECK(!timerAdjust(&timer, 1));
  timerReset(&timer);
  PanelState batteryState = {0};
  moduleBattery(&cfg, &batteryState);
  if (access("/sys/class/power_supply", R_OK) == 0)
    CHECK(batteryState.battery[0] != '\0');

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
