#ifndef SLIVERBAR_APP_LAUNCHER_H
#define SLIVERBAR_APP_LAUNCHER_H

#include "panel.h"

typedef enum {
  APP_ROLE_SYSTEM_MONITOR,
  APP_ROLE_NETWORK_SETTINGS,
  APP_ROLE_VOLUME_SETTINGS,
  APP_ROLE_CALENDAR,
} AppRole;

bool appLauncherHasGio(void);
const char *appRoleName(AppRole role);
bool appRoleAvailable(const PanelConfig *config, AppRole role);
int appLaunchRole(const PanelConfig *config, AppRole role);
bool appTerminalAvailable(const PanelConfig *config);
int appLaunchTerminal(const PanelConfig *config, char *const command[]);
bool appSpecAvailable(const PanelConfig *config, const char *spec);
int appLaunchSpec(const PanelConfig *config, const char *spec);
bool appCanOpenFile(const char *path);
int appOpenFile(const char *path);

#endif
