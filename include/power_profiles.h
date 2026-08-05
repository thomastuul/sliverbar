#ifndef SLIVERBAR_POWER_PROFILES_H
#define SLIVERBAR_POWER_PROFILES_H

#include <stdbool.h>
#include <stddef.h>

#include "panel.h"

#define POWER_PROFILE_MAX 8
#define POWER_PROFILE_ID_MAX 64

typedef struct {
  char id[POWER_PROFILE_ID_MAX];
  char label[64];
  bool active;
} PowerProfile;

typedef struct {
  bool available;
  char active[POWER_PROFILE_ID_MAX];
  PowerProfile profiles[POWER_PROFILE_MAX];
  size_t count;
} PowerProfileState;

typedef int (*PowerProfilesQueryFunction)(const PanelConfig *config,
                                          PowerProfileState *state,
                                          void *context);
typedef int (*PowerProfileSetFunction)(const char *id,
                                       char *error,
                                       size_t errorSize,
                                       void *context);

typedef struct {
  PowerProfilesQueryFunction query;
  PowerProfileSetFunction set;
  void *context;
} PowerProfilesBackend;

const char *powerProfileLabel(const PanelConfig *config, const char *id);
bool powerProfileIdValid(const char *id);
bool powerProfileOffered(const PowerProfileState *state, const char *id);
int powerProfilesQuery(const PanelConfig *config, PowerProfileState *state);
int powerProfileSet(const char *id, char *error, size_t errorSize);
const char *powerProfilesBackendName(const PowerProfileState *state);
void powerProfilesSetBackendForTests(const PowerProfilesBackend *backend);

#endif
