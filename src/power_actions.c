#include "power_actions.h"

#include "panel.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_GIO
#include <gio/gio.h>
#endif

typedef struct {
  const char *id;
  const char *glyph;
  const char *labelEn;
  const char *labelDe;
  const char *canMethod;
  const char *method;
} PowerDefinition;

static const PowerDefinition DEFINITIONS[] = {
    {"lock", "", "Lock screen", "Bildschirm sperren", NULL, "LockSessions"},
    {"suspend", "", "Suspend", "Standby", "CanSuspend", "Suspend"},
    {"hibernate",
     "",
     "Hibernate",
     "Ruhezustand",
     "CanHibernate",
     "Hibernate"},
    {"suspend_then_hibernate",
     " ",
     "Suspend, then hibernate",
     "Standby, dann Ruhezustand",
     "CanSuspendThenHibernate",
     "SuspendThenHibernate"},
    {"hybrid_sleep",
     "",
     "Hybrid sleep",
     "Hybrider Schlafmodus",
     "CanHybridSleep",
     "HybridSleep"},
    {"reboot", "", "Restart", "Neustart", "CanReboot", "Reboot"},
    {"poweroff", "", "Power off", "Ausschalten", "CanPowerOff", "PowerOff"},
};

#ifdef HAVE_GIO
static GDBusProxy *loginProxy(void) {
  GError *error = NULL;
  GDBusProxy *proxy =
      g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                    NULL,
                                    "org.freedesktop.login1",
                                    "/org/freedesktop/login1",
                                    "org.freedesktop.login1.Manager",
                                    NULL,
                                    &error);
  if (error)
    g_error_free(error);
  return proxy;
}

static bool sessionLockerAvailable(void) {
  static const char *const NAMES[] = {"org.freedesktop.ScreenSaver",
                                      "org.gnome.ScreenSaver",
                                      "org.kde.screensaver"};
  GError *error = NULL;
  GDBusConnection *connection =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (error)
    g_error_free(error);
  if (!connection)
    return false;
  bool available = false;
  for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
    GVariant *reply =
        g_dbus_connection_call_sync(connection,
                                    "org.freedesktop.DBus",
                                    "/org/freedesktop/DBus",
                                    "org.freedesktop.DBus",
                                    "NameHasOwner",
                                    g_variant_new("(s)", NAMES[i]),
                                    G_VARIANT_TYPE("(b)"),
                                    G_DBUS_CALL_FLAGS_NONE,
                                    500,
                                    NULL,
                                    NULL);
    gboolean owned = false;
    if (reply) {
      g_variant_get(reply, "(b)", &owned);
      g_variant_unref(reply);
    }
    if (owned) {
      available = true;
      break;
    }
  }
  g_object_unref(connection);
  return available;
}

static bool capability(GDBusProxy *proxy,
                       const char *method,
                       char *authorization,
                       size_t authorizationSize) {
  if (!method) {
    if (authorization)
      snprintf(authorization, authorizationSize, "yes");
    return true;
  }
  GError *error = NULL;
  GVariant *reply = g_dbus_proxy_call_sync(
      proxy, method, NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
  if (error)
    g_error_free(error);
  if (!reply)
    return false;
  const char *answer = NULL;
  g_variant_get(reply, "(&s)", &answer);
  bool allowed =
      answer && (!strcmp(answer, "yes") || !strcmp(answer, "challenge"));
  if (authorization)
    snprintf(authorization,
             authorizationSize,
             "%s",
             answer ? answer : "unavailable");
  g_variant_unref(reply);
  return allowed;
}
#endif

bool powerActionAllowed(const char *selection, const char *id) {
  if (!selection || !id || !*id)
    return false;
  const char *cursor = selection;
  size_t idLength = strlen(id);
  while (*cursor) {
    while (*cursor == ',' || *cursor == ' ' || *cursor == '\t')
      cursor++;
    const char *end = strchr(cursor, ',');
    size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
    while (length && (cursor[length - 1] == ' ' || cursor[length - 1] == '\t'))
      length--;
    if (length == idLength && !memcmp(cursor, id, length))
      return true;
    cursor = end ? end + 1 : cursor + length;
  }
  return false;
}

const char *powerActionLabel(const PanelConfig *config, const char *id) {
  for (size_t i = 0; i < sizeof(DEFINITIONS) / sizeof(DEFINITIONS[0]); i++)
    if (!strcmp(id, DEFINITIONS[i].id))
      return panelLanguageIsGerman(config) ? DEFINITIONS[i].labelDe
                                           : DEFINITIONS[i].labelEn;
  return id;
}

size_t powerActionList(const PanelConfig *config,
                       const char *selection,
                       PowerAction *actions,
                       size_t capacity) {
  if (!actions || capacity == 0)
    return 0;
#ifdef HAVE_GIO
  GDBusProxy *proxy = loginProxy();
  if (!proxy)
    return 0;
  size_t count = 0;
  const char *cursor = selection;
  while (cursor && *cursor && count < capacity) {
    while (*cursor == ',' || *cursor == ' ' || *cursor == '\t')
      cursor++;
    if (!*cursor)
      break;
    const char *end = strchr(cursor, ',');
    size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
    while (length && (cursor[length - 1] == ' ' || cursor[length - 1] == '\t'))
      length--;
    const PowerDefinition *definition = NULL;
    for (size_t i = 0; i < sizeof(DEFINITIONS) / sizeof(DEFINITIONS[0]); i++)
      if (strlen(DEFINITIONS[i].id) == length &&
          !memcmp(DEFINITIONS[i].id, cursor, length)) {
        definition = &DEFINITIONS[i];
        break;
      }
    cursor = end ? end + 1 : cursor + length;
    if (!definition ||
        (!strcmp(definition->id, "lock") && !sessionLockerAvailable()))
      continue;
    if (!capability(proxy,
                    definition->canMethod,
                    actions[count].authorization,
                    sizeof(actions[count].authorization)))
      continue;
    snprintf(
        actions[count].id, sizeof(actions[count].id), "%s", definition->id);
    snprintf(actions[count].glyph,
             sizeof(actions[count].glyph),
             "%s",
             definition->glyph);
    snprintf(actions[count].label,
             sizeof(actions[count].label),
             "%s",
             panelLanguageIsGerman(config) ? definition->labelDe
                                           : definition->labelEn);
    count++;
  }
  g_object_unref(proxy);
  return count;
#else
  (void)config;
  (void)selection;
  (void)capacity;
  return 0;
#endif
}

int powerActionExecute(const char *id) {
#ifdef HAVE_GIO
  const PowerDefinition *definition = NULL;
  for (size_t i = 0; i < sizeof(DEFINITIONS) / sizeof(DEFINITIONS[0]); i++)
    if (!strcmp(id, DEFINITIONS[i].id)) {
      definition = &DEFINITIONS[i];
      break;
    }
  if (!definition)
    return -1;
  if (!strcmp(definition->id, "lock") && !sessionLockerAvailable())
    return -1;
  GDBusProxy *proxy = loginProxy();
  if (!proxy || !capability(proxy, definition->canMethod, NULL, 0)) {
    if (proxy)
      g_object_unref(proxy);
    return -1;
  }
  GVariant *parameters =
      definition->canMethod ? g_variant_new("(b)", true) : NULL;
  GError *error = NULL;
  GVariant *reply = g_dbus_proxy_call_sync(proxy,
                                           definition->method,
                                           parameters,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &error);
  if (error) {
    logMessage("ERROR", "power action %s failed: %s", id, error->message);
    g_error_free(error);
  }
  bool succeeded = reply != NULL;
  if (reply)
    g_variant_unref(reply);
  g_object_unref(proxy);
  return succeeded ? 0 : -1;
#else
  (void)id;
  return -1;
#endif
}
