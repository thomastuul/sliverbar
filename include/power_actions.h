#ifndef SLIVERBAR_POWER_ACTIONS_H
#define SLIVERBAR_POWER_ACTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "panel.h"

typedef struct {
  char id[32];
  char glyph[16];
  char label[64];
  char authorization[16];
} PowerAction;

bool powerActionAllowed(const char *selection, const char *id);
size_t powerActionList(const PanelConfig *config,
                       const char *selection,
                       PowerAction *actions,
                       size_t capacity);
const char *powerActionLabel(const PanelConfig *config, const char *id);
int powerActionExecute(const char *id);

#endif
