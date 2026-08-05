#include "power_profiles.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_GIO
#include <gio/gio.h>
#endif

#define POWER_PROFILES_NAME "net.hadess.PowerProfiles"
#define POWER_PROFILES_PATH "/net/hadess/PowerProfiles"
#define POWER_PROFILES_INTERFACE "net.hadess.PowerProfiles"

static const PowerProfilesBackend *testBackend;

void powerProfilesSetBackendForTests(const PowerProfilesBackend *backend) {
  testBackend = backend;
}

const char *powerProfileLabel(const PanelConfig *config, const char *id) {
  bool german = panelLanguageIsGerman(config);
  if (!strcmp(id, "power-saver"))
    return german ? "Energiesparen" : "Power saver";
  if (!strcmp(id, "balanced"))
    return german ? "Ausgeglichen" : "Balanced";
  if (!strcmp(id, "performance"))
    return german ? "Leistung" : "Performance";
  return id;
}

bool powerProfileIdValid(const char *id) {
  if (!id || !*id || strlen(id) >= POWER_PROFILE_ID_MAX)
    return false;
  for (const unsigned char *character = (const unsigned char *)id; *character;
       character++)
    if (!islower(*character) && !isdigit(*character) && *character != '-' &&
        *character != '_')
      return false;
  return true;
}

bool powerProfileOffered(const PowerProfileState *state, const char *id) {
  if (!state || !id || !*id)
    return false;
  for (size_t i = 0; i < state->count; i++)
    if (!strcmp(state->profiles[i].id, id))
      return true;
  return false;
}

const char *powerProfilesBackendName(const PowerProfileState *state) {
  return state && state->available ? "power-profiles-daemon-dbus"
                                   : "unavailable";
}

#ifdef HAVE_GIO
static GDBusProxy *powerProfilesProxy(GError **error) {
  return g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       NULL,
                                       POWER_PROFILES_NAME,
                                       POWER_PROFILES_PATH,
                                       POWER_PROFILES_INTERFACE,
                                       NULL,
                                       error);
}

static void copyError(char *out, size_t size, const GError *error) {
  if (out && size)
    snprintf(out,
             size,
             "%s",
             error && error->message ? error->message : "unknown D-Bus error");
}
#endif

static int powerProfilesQueryDbus(const PanelConfig *config,
                                  PowerProfileState *state) {
  if (!state)
    return -1;
  memset(state, 0, sizeof(*state));
#ifdef HAVE_GIO
  GError *error = NULL;
  GDBusProxy *proxy = powerProfilesProxy(&error);
  if (!proxy) {
    if (error)
      g_error_free(error);
    return -1;
  }
  GVariant *active = g_dbus_proxy_get_cached_property(proxy, "ActiveProfile");
  GVariant *profiles = g_dbus_proxy_get_cached_property(proxy, "Profiles");
  if (!active || !profiles ||
      !g_variant_is_of_type(active, G_VARIANT_TYPE_STRING) ||
      !g_variant_is_of_type(profiles, G_VARIANT_TYPE("aa{sv}"))) {
    if (active)
      g_variant_unref(active);
    if (profiles)
      g_variant_unref(profiles);
    g_object_unref(proxy);
    return -1;
  }
  snprintf(state->active,
           sizeof(state->active),
           "%s",
           g_variant_get_string(active, NULL));
  GVariantIter iterator;
  GVariant *entry = NULL;
  g_variant_iter_init(&iterator, profiles);
  while (state->count < POWER_PROFILE_MAX &&
         g_variant_iter_next(&iterator, "@a{sv}", &entry)) {
    const char *id = NULL;
    if (g_variant_lookup(entry, "Profile", "&s", &id) &&
        powerProfileIdValid(id)) {
      PowerProfile *profile = &state->profiles[state->count++];
      snprintf(profile->id, sizeof(profile->id), "%s", id);
      snprintf(profile->label,
               sizeof(profile->label),
               "%s",
               powerProfileLabel(config, id));
      profile->active = !strcmp(id, state->active);
    }
    g_variant_unref(entry);
  }
  state->available = state->count > 0;
  g_variant_unref(active);
  g_variant_unref(profiles);
  g_object_unref(proxy);
  return state->available ? 0 : -1;
#else
  (void)config;
  return -1;
#endif
}

int powerProfilesQuery(const PanelConfig *config, PowerProfileState *state) {
  if (testBackend && testBackend->query)
    return testBackend->query(config, state, testBackend->context);
  return powerProfilesQueryDbus(config, state);
}

static int
powerProfileSetDbus(const char *id, char *errorText, size_t errorSize) {
  if (errorText && errorSize)
    errorText[0] = '\0';
#ifdef HAVE_GIO
  if (!powerProfileIdValid(id)) {
    if (errorText && errorSize)
      snprintf(errorText, errorSize, "invalid profile identifier");
    return -1;
  }
  PowerProfileState state;
  if (powerProfilesQuery(NULL, &state) || !powerProfileOffered(&state, id)) {
    if (errorText && errorSize)
      snprintf(errorText, errorSize, "profile is not offered by the service");
    return -1;
  }
  GError *error = NULL;
  GDBusProxy *proxy = powerProfilesProxy(&error);
  if (!proxy) {
    copyError(errorText, errorSize, error);
    if (error)
      g_error_free(error);
    return -1;
  }
  GVariant *reply = g_dbus_connection_call_sync(
      g_dbus_proxy_get_connection(proxy),
      POWER_PROFILES_NAME,
      POWER_PROFILES_PATH,
      "org.freedesktop.DBus.Properties",
      "Set",
      g_variant_new("(ssv)",
                    POWER_PROFILES_INTERFACE,
                    "ActiveProfile",
                    g_variant_new_string(id)),
      NULL,
      G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
      5000,
      NULL,
      &error);
  if (!reply) {
    copyError(errorText, errorSize, error);
    if (error)
      g_error_free(error);
    g_object_unref(proxy);
    return -1;
  }
  g_variant_unref(reply);
  g_object_unref(proxy);
  PowerProfileState confirmed;
  if (powerProfilesQuery(NULL, &confirmed) ||
      strcmp(confirmed.active, id) != 0) {
    if (errorText && errorSize)
      snprintf(errorText, errorSize, "service did not confirm the profile");
    return -1;
  }
  return 0;
#else
  (void)id;
  if (errorText && errorSize)
    snprintf(errorText, errorSize, "power profile support is not compiled in");
  return -1;
#endif
}

int powerProfileSet(const char *id, char *errorText, size_t errorSize) {
  if (testBackend && testBackend->set)
    return testBackend->set(id, errorText, errorSize, testBackend->context);
  return powerProfileSetDbus(id, errorText, errorSize);
}
