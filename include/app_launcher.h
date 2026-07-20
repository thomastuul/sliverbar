#ifndef SLIVERBAR_APP_LAUNCHER_H
#define SLIVERBAR_APP_LAUNCHER_H

#include "panel.h"

typedef enum {
  APP_ROLE_SYSTEM_MONITOR,
  APP_ROLE_NETWORK_SETTINGS,
  APP_ROLE_VOLUME_SETTINGS,
  APP_ROLE_CALENDAR,
} AppRole;

typedef struct {
  char name[160];
  char search[512];
  char desktopId[256];
} AppEntry;

bool appLauncherHasGio(void);
const char *appRoleName(AppRole role);
bool appRoleAvailable(const PanelConfig *config, AppRole role);
int appLaunchRole(const PanelConfig *config, AppRole role);
void appDescribeRole(const PanelConfig *config,
                     AppRole role,
                     char *output,
                     size_t outputSize);
bool appTerminalAvailable(const PanelConfig *config);
int appLaunchTerminal(const PanelConfig *config, char *const command[]);
void appDescribeTerminal(const PanelConfig *config,
                         char *output,
                         size_t outputSize);
bool appSpecAvailable(const PanelConfig *config, const char *spec);
int appLaunchSpec(const PanelConfig *config, const char *spec);
bool appCanOpenFile(const char *path);
int appOpenFile(const char *path);
size_t appCatalogLoad(AppEntry *entries, size_t capacity);
int appSearchRank(const char *label, const char *search, const char *query);

#endif
