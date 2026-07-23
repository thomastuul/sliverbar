#ifndef SLIVERBAR_AGENDA_PROVIDER_EDS_H
#define SLIVERBAR_AGENDA_PROVIDER_EDS_H

#include <libecal/libecal.h>

#include <time.h>

time_t agendaProviderTimeToEpoch(const ICalTime *value,
                                 const ICalTimezone *defaultTimezone);

#endif
