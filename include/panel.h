#ifndef SLIVERBAR_PANEL_H
#define SLIVERBAR_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PANEL_TEXT_MAX 4096
#define PANEL_PATH_MAX 4096
#define PANEL_ARG_MAX 16
#define PANEL_WORKSPACE_MAX 32

typedef enum {
  MODULE_AUTO,
  MODULE_ENABLED,
  MODULE_DISABLED,
} ModuleMode;

typedef struct {
  char font[128], iconFont[128], geometry[64], wmName[64];
  char workspaceBackend[16];
  char terminal[64], location[128], language[16];
  char launcher[PANEL_PATH_MAX], powerMenu[PANEL_PATH_MAX];
  char weatherCache[PANEL_PATH_MAX], weatherImage[PANEL_PATH_MAX];
  char colorPanelBg[16], colorBg[16], colorFg[16], colorFree[16],
      colorFocus[16];
  char colorFreeBg[16], colorFocusedFree[16], colorFocusedFreeBg[16];
  char colorOccupied[16], colorOccupiedBg[16];
  char colorFocusedOccupied[16], colorFocusedOccupiedBg[16];
  char colorUrgent[16], colorUrgentBg[16];
  char colorFocusedUrgent[16], colorFocusedUrgentBg[16];
  char colorClock[16], colorVolume[16], colorMuted[16], colorSystem[16];
  char colorNetwork[16], colorWeather[16], colorBattery[16];
  char colorWarning[16], colorCritical[16], colorBrightness[16];
  int height, clickableAreas, underline, volumeStep, brightnessStep;
  unsigned weatherInterval, networkInterval, titleMax;
  ModuleMode moduleClock, moduleTitle, moduleCpu, moduleBattery;
  ModuleMode moduleScreencast, moduleVolume, moduleNetwork, moduleBrightness;
  ModuleMode moduleWeather, moduleLauncher, moduleTray, modulePower;
} PanelConfig;

typedef struct {
  uint64_t cpuTotal, cpuIdle;
  bool cpuInitialized;
  int brightnessPercent;
  bool brightnessInitialized;
  bool focusedWorkspaceKnown;
  bool focusedWorkspaceOccupied;
  char brightnessOutput[64];
  char workspace[PANEL_TEXT_MAX], title[PANEL_TEXT_MAX];
  char launcher[PANEL_TEXT_MAX], weather[PANEL_TEXT_MAX],
      battery[PANEL_TEXT_MAX];
  char network[PANEL_TEXT_MAX], brightness[PANEL_TEXT_MAX],
      volume[PANEL_TEXT_MAX];
  char cpu[PANEL_TEXT_MAX], clock[PANEL_TEXT_MAX], tray[PANEL_TEXT_MAX];
  char power[PANEL_TEXT_MAX], screencast[PANEL_TEXT_MAX];
} PanelState;

typedef struct {
  size_t count;
  size_t current;
  bool occupied[PANEL_WORKSPACE_MAX];
  bool urgent[PANEL_WORKSPACE_MAX];
  char names[PANEL_WORKSPACE_MAX][64];
} WorkspaceSnapshot;

void configDefaults(PanelConfig *cfg);
bool moduleModeActive(ModuleMode mode, bool available);
int configLoad(PanelConfig *cfg,
               const char *path,
               char *error,
               size_t errorSize);
int mkdirP(const char *path, mode_t mode);
int readTextFile(const char *path, char *buffer, size_t size);
int writeAtomic(const char *path, const char *data, mode_t mode);
bool commandExists(const char *name);
int runCapture(char *const argv[], char *output, size_t size, int timeoutMs);
int spawnDetached(char *const argv[]);
void shellQuoteAction(const char *input, char *output, size_t size);
void logMessage(const char *level, const char *format, ...);

void moduleClock(const PanelConfig *cfg, PanelState *state);
void moduleCpu(const PanelConfig *cfg, PanelState *state);
void moduleBattery(const PanelConfig *cfg, PanelState *state);
void moduleScreencast(const PanelConfig *cfg,
                      PanelState *state,
                      const char *runtimeDir);
void moduleVolume(const PanelConfig *cfg, PanelState *state);
int parseNmcliWifi(const char *output,
                   char *ssid,
                   size_t ssidSize,
                   int *strength);
int wifiQualityPercent(double quality);
int parseDefaultRouteInterface(const char *routes,
                               char *interface,
                               size_t interfaceSize);
void moduleNetwork(const PanelConfig *cfg, PanelState *state);
void moduleBrightness(const PanelConfig *cfg, PanelState *state);
void moduleBrightnessValue(const PanelConfig *cfg,
                           PanelState *state,
                           int percent);
void moduleWeather(const PanelConfig *cfg, PanelState *state);
void moduleWorkspace(const PanelConfig *cfg,
                     PanelState *state,
                     const char *report);
void moduleWorkspaceEwmh(const PanelConfig *cfg,
                         PanelState *state,
                         const WorkspaceSnapshot *snapshot);
void moduleStatic(const PanelConfig *cfg, PanelState *state);
void renderPanel(const PanelState *state, char *output, size_t size);

#endif
