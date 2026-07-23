#include "agenda_provider_eds.h"

#include <stdio.h>
#include <string.h>

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

static time_t utcEpoch(const char *value) {
  ICalTime *time = i_cal_time_new_from_string(value);
  if (!time)
    return 0;
  time_t epoch = i_cal_time_as_timet(time);
  g_object_unref(time);
  return epoch;
}

static int checkBerlinTime(const char *localValue,
                           const char *utcValue,
                           time_t rawOffset) {
  ICalTimezone *berlin = i_cal_timezone_get_builtin_timezone("Europe/Berlin");
  CHECK(berlin != NULL);

  ICalTime *zoned = i_cal_time_new_from_string(localValue);
  CHECK(zoned != NULL);
  i_cal_time_set_timezone(zoned, berlin);
  time_t expected = utcEpoch(utcValue);
  CHECK(expected != 0);
  CHECK(agendaProviderTimeToEpoch(zoned, NULL) == expected);
  CHECK(i_cal_time_as_timet(zoned) - expected == rawOffset);
  g_object_unref(zoned);

  ICalTime *floating = i_cal_time_new_from_string(localValue);
  CHECK(floating != NULL);
  CHECK(i_cal_time_get_timezone(floating) == NULL);
  CHECK(agendaProviderTimeToEpoch(floating, berlin) == expected);
  g_object_unref(floating);
  return 0;
}

int main(void) {
  CHECK(checkBerlinTime(
            "20260723T140000", "20260723T120000Z", (time_t)2 * 60 * 60) == 0);
  CHECK(checkBerlinTime(
            "20260123T140000", "20260123T130000Z", (time_t)60 * 60) == 0);

  ICalTime *utc = i_cal_time_new_from_string("20260723T120000Z");
  CHECK(utc != NULL);
  CHECK(agendaProviderTimeToEpoch(
            utc, i_cal_timezone_get_builtin_timezone("Europe/Berlin")) ==
        utcEpoch("20260723T120000Z"));
  g_object_unref(utc);

  ECalComponent *component = e_cal_component_new_from_string(
      "BEGIN:VEVENT\r\n"
      "UID:organizer-test\r\n"
      "DTSTART:20260723T120000Z\r\n"
      "DTEND:20260723T130000Z\r\n"
      "ORGANIZER;CN=Max Mustermann:mailto:max@example.com\r\n"
      "END:VEVENT\r\n");
  CHECK(component != NULL);
  char organizer[256];
  agendaProviderOrganizerLabel(component, organizer, sizeof(organizer));
  CHECK(strcmp(organizer, "Max Mustermann") == 0);
  g_object_unref(component);

  component =
      e_cal_component_new_from_string("BEGIN:VEVENT\r\n"
                                      "UID:organizer-mail-test\r\n"
                                      "DTSTART:20260723T120000Z\r\n"
                                      "DTEND:20260723T130000Z\r\n"
                                      "ORGANIZER:mailto:other@example.com\r\n"
                                      "END:VEVENT\r\n");
  CHECK(component != NULL);
  agendaProviderOrganizerLabel(component, organizer, sizeof(organizer));
  CHECK(strcmp(organizer, "other@example.com") == 0);
  g_object_unref(component);
  return 0;
}
