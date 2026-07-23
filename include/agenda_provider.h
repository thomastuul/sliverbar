#ifndef SLIVERBAR_AGENDA_PROVIDER_H
#define SLIVERBAR_AGENDA_PROVIDER_H

#include "agenda.h"
#include "panel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct AgendaProvider AgendaProvider;

typedef struct {
  size_t selectedSources;
  size_t reachableSources;
  size_t failedSources;
  bool initialized;
} AgendaProviderStatus;

bool agendaProviderEdsCompiled(void);
int agendaProviderListSources(FILE *output, FILE *errorOutput);
AgendaProvider *agendaProviderCreate(const PanelConfig *config);
int agendaProviderStart(AgendaProvider *provider);
int agendaProviderPollFd(const AgendaProvider *provider);
bool agendaProviderRead(AgendaProvider *provider,
                        AgendaSnapshot *snapshot,
                        AgendaProviderStatus *status);
void agendaProviderDestroy(AgendaProvider *provider);

#endif
