#include "panel.h"

#include "app_launcher.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
  CHECK(strstr(cfg.font, "Monospace") != NULL);
  CHECK(cfg.iconFont[0] == '\0');
  CHECK(strcmp(cfg.terminal, "auto") == 0);
  CHECK(cfg.location[0] == '\0');
  CHECK(cfg.moduleClock == MODULE_AUTO);
  CHECK(moduleModeActive(MODULE_AUTO, true));
  CHECK(!moduleModeActive(MODULE_AUTO, false));
  CHECK(moduleModeActive(MODULE_ENABLED, false));
  CHECK(!moduleModeActive(MODULE_DISABLED, true));

  char path[] = "/tmp/sliverbar-test-XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  const char TEXT[] =
      "height=31\nvolume_step=4\ncolor_bg=#000000\nmodule_cpu=disabled\n";
  CHECK(write(fd, TEXT, sizeof(TEXT) - 1) == (ssize_t)(sizeof(TEXT) - 1));
  CHECK(close(fd) == 0);

  char error[256];
  CHECK(configLoad(&cfg, path, error, sizeof(error)) == 0);
  CHECK(cfg.height == 31);
  CHECK(cfg.volumeStep == 4);
  CHECK(strcmp(cfg.colorBg, "#000000") == 0);
  CHECK(strcmp(cfg.colorOccupied, "#ff5555") == 0);
  CHECK(cfg.moduleCpu == MODULE_DISABLED);
  CHECK(unlink(path) == 0);

  CHECK(appSpecAvailable(&cfg, "command:/bin/true"));
  CHECK(!appSpecAvailable(&cfg, "command:/definitely/missing/sliverbar"));
  CHECK(!appSpecAvailable(&cfg, "command:'unterminated"));
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
    const char *oldDataHome = getenv("XDG_DATA_HOME");
    char oldDataHomeCopy[PANEL_PATH_MAX] = "";
    if (oldDataHome)
      snprintf(oldDataHomeCopy, sizeof(oldDataHomeCopy), "%s", oldDataHome);
    CHECK(setenv("XDG_DATA_HOME", dataHome, 1) == 0);
    CHECK(appSpecAvailable(&cfg, "desktop:sliverbar-test.desktop"));
    CHECK(appLaunchSpec(&cfg, "desktop:sliverbar-test.desktop") == 0);
    if (oldDataHome)
      CHECK(setenv("XDG_DATA_HOME", oldDataHomeCopy, 1) == 0);
    else
      CHECK(unsetenv("XDG_DATA_HOME") == 0);
    CHECK(unlink(desktopPath) == 0);
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

  PanelState brightnessState = {0};
  moduleBrightnessValue(&cfg, &brightnessState, 42);
  CHECK(strstr(brightnessState.brightness, "  42%") != NULL);

  PanelState state = {0};
  strcpy(state.launcher, "L");
  strcpy(state.workspace, "W");
  strcpy(state.title, "T");
  strcpy(state.clock, "C");
  char rendered[128];
  renderPanel(&state, rendered, sizeof(rendered));
  CHECK(strcmp(rendered, "%{l}LW%{c}T%{r}C\n") == 0);

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
  CHECK(wifiQualityPercent(51.0) == 73);
  CHECK(wifiQualityPercent(0.0) == 0);
  CHECK(wifiQualityPercent(70.0) == 100);
  CHECK(wifiQualityPercent(90.0) == 100);
  CHECK(wifiQualityPercent(-1.0) == -1);

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
