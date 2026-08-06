#ifndef SLIVERBAR_PANEL_H
#define SLIVERBAR_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "timer.h"

#define PANEL_TEXT_MAX 4096
#define PANEL_PATH_MAX 4096
#define PANEL_ARG_MAX 16
#define PANEL_WORKSPACE_MAX 32
#define PANEL_WEATHER_LOCATION_MAX 4
#define PANEL_WEATHER_INTERVAL_MIN 1800U
#define PANEL_WEATHER_INTERVAL_MAX 14400U
#define PANEL_AGENDA_SOURCE_MAX 16
#define PANEL_AGENDA_SOURCE_ID_MAX 256
#define PANEL_AGENDA_DAYS_MIN 1U
#define PANEL_AGENDA_DAYS_MAX 31U
#define PANEL_AGENDA_ITEMS_MIN 4U
#define PANEL_AGENDA_ITEMS_MAX 20U
#define PANEL_AGENDA_UNDATED_MAX 5U
#define PANEL_AGENDA_REFRESH_MIN 60U
#define PANEL_AGENDA_REFRESH_MAX 3600U
#define PANEL_AGENDA_WIDTH_MIN 320U
#define PANEL_AGENDA_WIDTH_MAX 720U
#define WIFI_NETWORK_MAX 3

typedef enum {
  MODULE_AUTO,
  MODULE_ENABLED,
  MODULE_DISABLED,
} ModuleMode;

typedef struct {
  char id[64];
  char label[128];
  char query[128];
} WeatherLocation;

typedef enum {
  AGENDA_SOURCES_ALL,
  AGENDA_SOURCES_NONE,
  AGENDA_SOURCES_EXPLICIT,
} AgendaSourceMode;

typedef struct {
  char font[128], iconFont[128], geometry[64], wmName[64], monitor[64];
  char workspaceBackend[16];
  char applicationLauncher[16], powerMenuMode[16];
  char powerActions[256], powerConfirm[256];
  char terminal[64], location[128], language[16];
  char systemMonitor[256], networkSettings[256], volumeSettings[256];
  char calendar[256], tasks[256];
  char agendaProvider[16];
  char agendaCalendarSources[PANEL_AGENDA_SOURCE_MAX]
                            [PANEL_AGENDA_SOURCE_ID_MAX];
  char agendaTaskSources[PANEL_AGENDA_SOURCE_MAX][PANEL_AGENDA_SOURCE_ID_MAX];
  char timerSound[PANEL_PATH_MAX];
  char launcher[PANEL_PATH_MAX], powerMenu[PANEL_PATH_MAX];
  char weatherCache[PANEL_PATH_MAX], weatherImage[PANEL_PATH_MAX];
  char weatherCacheRoot[PANEL_PATH_MAX], weatherState[PANEL_PATH_MAX];
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
  char agendaEventColor[16], agendaTaskColor[16], agendaOverdueColor[16],
      agendaSourceColor[16];
  int height, clickableAreas, underline, blockPadding, volumeStep;
  int brightnessStep;
  unsigned weatherInterval, networkInterval, titleMax;
  unsigned agendaDays, agendaMaxItems, agendaMaxUndatedTasks;
  unsigned agendaRefreshInterval, agendaPopupWidth;
  bool agendaShowSource;
  AgendaSourceMode agendaCalendarSourceMode, agendaTaskSourceMode;
  bool agendaCalendarSourceConfigured, agendaTaskSourceConfigured;
  size_t agendaCalendarSourceCount, agendaTaskSourceCount;
  ModuleMode moduleClock, moduleTitle, moduleCpu, moduleBattery;
  ModuleMode moduleScreencast, moduleVolume, moduleNetwork, moduleBrightness;
  ModuleMode moduleWeather, moduleLauncher, moduleTray, modulePower;
  ModuleMode moduleInhibitor, moduleTimer;
  bool internalLauncherAvailable, internalPowerAvailable;
  bool internalPowerProfilesAvailable;
  bool internalBatteryDetailsAvailable;
  bool internalNetworkMenuAvailable;
  bool internalWeatherForecastAvailable;
  bool internalAgendaAvailable;
  WeatherLocation weatherLocations[PANEL_WEATHER_LOCATION_MAX];
  size_t weatherLocationCount, activeWeatherLocation;
  char defaultWeatherLocation[64];
} PanelConfig;

typedef struct {
  bool available;
  bool capacityValid;
  int capacityPercent;
  bool statusValid;
  char status[32];
  bool chargeNowValid;
  uint64_t chargeNow;
  bool chargeFullValid;
  uint64_t chargeFull;
  bool chargeFullDesignValid;
  uint64_t chargeFullDesign;
  bool healthValid;
  double healthPercent;
  bool currentNowValid;
  int64_t currentNow;
} BatteryDetails;

typedef struct {
  uint64_t cpuTotal, cpuIdle;
  bool cpuInitialized;
  int brightnessPercent;
  bool brightnessInitialized;
  bool brightnessUpdatePending;
  bool brightnessHardware;
  unsigned brightnessMaximum;
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
  char inhibitor[PANEL_TEXT_MAX], timer[PANEL_TEXT_MAX];
  BatteryDetails batteryDetails;
} PanelState;

typedef struct {
  size_t count;
  size_t current;
  bool occupied[PANEL_WORKSPACE_MAX];
  bool urgent[PANEL_WORKSPACE_MAX];
  char names[PANEL_WORKSPACE_MAX][64];
} WorkspaceSnapshot;

typedef struct {
  char interface[128];
  char backend[32];
  double rawValue;
  int percent;
} WifiDiagnostic;

typedef struct {
  char ssid[128];
  char uuid[64];
  int strength;
  bool active;
} WifiNetwork;

typedef struct {
  WifiNetwork networks[WIFI_NETWORK_MAX];
  size_t count;
} WifiNetworkList;

void configDefaults(PanelConfig *cfg);
const char *panelLanguage(const PanelConfig *cfg);
bool panelLanguageIsGerman(const PanelConfig *cfg);
bool moduleModeActive(ModuleMode mode, bool available);
int configLoad(PanelConfig *cfg,
               const char *path,
               char *error,
               size_t errorSize);
int mkdirP(const char *path, mode_t mode);
int sliverbarRuntimeBaseDirectory(char *path, size_t size, bool create);
int sliverbarRuntimeDirectory(char *path, size_t size, bool create);
int readTextFile(const char *path, char *buffer, size_t size);
int writeAtomic(const char *path, const char *data, mode_t mode);
bool commandExists(const char *name);
int runCapture(char *const argv[], char *output, size_t size, int timeoutMs);
int spawnDetached(char *const argv[]);
pid_t spawnTracked(char *const argv[]);
void shellQuoteAction(const char *input, char *output, size_t size);
void logMessage(const char *level, const char *format, ...);

const char *moduleClockGlyph(const PanelConfig *cfg, unsigned hour);
void moduleClock(const PanelConfig *cfg, PanelState *state);
void moduleCpu(const PanelConfig *cfg, PanelState *state);
const char *moduleBatteryStatusGlyph(const char *status);
bool batteryDetailsRead(const char *powerSupplyRoot, BatteryDetails *details);
void moduleBattery(const PanelConfig *cfg, PanelState *state);
void moduleScreencast(const PanelConfig *cfg,
                      PanelState *state,
                      const char *runtimeDir);
bool pactlVolumeArgument(const char *output,
                         int step,
                         const char *operation,
                         char *value,
                         size_t valueSize);
void moduleVolume(const PanelConfig *cfg, PanelState *state);
int parseNmcliWifi(const char *output,
                   char *ssid,
                   size_t ssidSize,
                   int *strength);
int wifiQualityPercent(double quality);
const char *wifiSignalGlyph(int strength);
int wifiKnownNetworks(WifiNetworkList *list);
pid_t wifiRequestScan(void);
pid_t wifiActivate(const char *uuid);
int parseWirelessQuality(const char *contents,
                         const char *interface,
                         double *quality,
                         int *percent);
int parseDefaultRouteInterface(const char *routes,
                               char *interface,
                               size_t interfaceSize);
int wifiDiagnostic(WifiDiagnostic *diagnostic);
void moduleNetwork(const PanelConfig *cfg, PanelState *state);
bool brightnessFactorFormat(int percent, char *value, size_t size);
bool brightnessFactorParse(const char *value, int *percent);
int brightnessPercentFromRaw(unsigned value, unsigned maximum);
unsigned brightnessRawFromPercent(int percent, unsigned maximum);
void moduleBrightness(const PanelConfig *cfg, PanelState *state);
int moduleBrightnessApply(const PanelState *state);
bool moduleBrightnessAdjust(const PanelConfig *cfg,
                            PanelState *state,
                            const char *operation);
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
void moduleInhibitor(const PanelConfig *cfg,
                     PanelState *state,
                     bool available,
                     bool active);
void moduleTimer(const PanelConfig *cfg,
                 PanelState *state,
                 unsigned minutes,
                 TimerDisplay display,
                 unsigned animationFrame);
void renderPanel(const PanelState *state, char *output, size_t size);

#endif
