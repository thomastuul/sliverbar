#ifndef SLIVERBAR_AGENDA_PROVIDER_EDS_H
#define SLIVERBAR_AGENDA_PROVIDER_EDS_H

#include <libecal/libecal.h>

#include <stddef.h>
#include <time.h>

time_t agendaProviderTimeToEpoch(const ICalTime *value,
                                 const ICalTimezone *defaultTimezone);
void agendaProviderOrganizerLabel(ECalComponent *component,
                                  char *output,
                                  size_t size);

#endif
