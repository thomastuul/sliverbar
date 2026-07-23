#include "agenda_provider.h"

#include <stdlib.h>

#ifdef HAVE_EDS

#include <errno.h>
#include <fcntl.h>
#include <libecal/libecal.h>
#include <libedataserver/libedataserver.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define AGENDA_PROVIDER_SOURCE_MAX 32U

typedef struct {
  AgendaProvider *provider;
  ECalClient *client;
  ECalClientView *view;
  char id[PANEL_AGENDA_SOURCE_ID_MAX];
  char name[AGENDA_SOURCE_NAME_MAX];
  AgendaItemType type;
  bool viewReady;
} OpenSource;

struct AgendaProvider {
  PanelConfig config;
  pthread_t thread;
  pthread_mutex_t mutex;
  AgendaSnapshot snapshot;
  AgendaProviderStatus status;
  GCancellable *cancellable;
  GMainContext *context;
  OpenSource sources[AGENDA_PROVIDER_SOURCE_MAX];
  size_t sourceCount;
  int notification[2];
  bool started;
  atomic_bool stopping;
  bool refreshRequested;
};

typedef struct {
  AgendaProvider *provider;
  AgendaSnapshot *snapshot;
  OpenSource *source;
} InstanceContext;

typedef struct {
  AgendaProvider *provider;
  GCancellable *cancellable;
  pthread_t thread;
  atomic_bool finished;
  bool started;
} SourceTimeout;

static void copyText(char *output, size_t size, const char *value) {
  if (!value || !*value) {
    output[0] = '\0';
    return;
  }
  if (!g_utf8_validate(value, -1, NULL)) {
    snprintf(output, size, "%s", "(Invalid UTF-8)");
    return;
  }
  size_t used = 0;
  bool space = false;
  for (const char *cursor = value; *cursor && used + 1 < size;
       cursor = g_utf8_next_char(cursor)) {
    gunichar character = g_utf8_get_char(cursor);
    if (character == '\n' || character == '\r' || character == '\t' ||
        g_unichar_isspace(character)) {
      space = used > 0;
      continue;
    }
    if (space && used + 1 < size)
      output[used++] = ' ';
    space = false;
    char encoded[6];
    int length = g_unichar_to_utf8(character, encoded);
    if (used + (size_t)length >= size)
      break;
    memcpy(output + used, encoded, (size_t)length);
    used += (size_t)length;
  }
  output[used] = '\0';
}

static bool
sourceSelected(const PanelConfig *config, AgendaItemType type, const char *id) {
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  AgendaSourceMode mode = type == AGENDA_ITEM_EVENT
                              ? config->agendaCalendarSourceMode
                              : config->agendaTaskSourceMode;
  if (mode == AGENDA_SOURCES_NONE)
    return false;
  if (mode == AGENDA_SOURCES_ALL)
    return true;
  size_t count = type == AGENDA_ITEM_EVENT ? config->agendaCalendarSourceCount
                                           : config->agendaTaskSourceCount;
  const char(*sources)[PANEL_AGENDA_SOURCE_ID_MAX] =
      type == AGENDA_ITEM_EVENT ? config->agendaCalendarSources
                                : config->agendaTaskSources;
  for (size_t index = 0; index < count; index++)
    if (!strcmp(sources[index], id))
      return true;
  return false;
}

static size_t configuredSourceCount(const PanelConfig *config,
                                    AgendaItemType type) {
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  AgendaSourceMode mode = type == AGENDA_ITEM_EVENT
                              ? config->agendaCalendarSourceMode
                              : config->agendaTaskSourceMode;
  if (mode == AGENDA_SOURCES_NONE)
    return 0;
  if (mode == AGENDA_SOURCES_EXPLICIT)
    return type == AGENDA_ITEM_EVENT ? config->agendaCalendarSourceCount
                                     : config->agendaTaskSourceCount;
  return SIZE_MAX;
}

static time_t localRangeEnd(unsigned days) {
  time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_mday += (int)days;
  local.tm_isdst = -1;
  return mktime(&local);
}

static bool appendComponent(AgendaSnapshot *snapshot,
                            OpenSource *source,
                            ECalComponent *component,
                            time_t start,
                            time_t end,
                            bool allDay) {
  if (snapshot->count >= AGENDA_ENTRY_MAX)
    return false;
  AgendaItem *item = &snapshot->items[snapshot->count];
  memset(item, 0, sizeof(*item));
  item->type = source->type;
  item->dateOnly = allDay;
  if (source->type == AGENDA_ITEM_EVENT) {
    item->start = start;
    item->end = end;
    item->hasStart = true;
    item->hasEnd = true;
  } else if (start != 0) {
    item->due = start;
    item->hasDue = true;
  }
  ICalPropertyStatus status = e_cal_component_get_status(component);
  item->cancelled = status == I_CAL_STATUS_CANCELLED;
  item->completed = status == I_CAL_STATUS_COMPLETED;
  const char *uid = e_cal_component_get_uid(component);
  char *rid = e_cal_component_get_recurid_as_string(component);
  snprintf(item->sourceId, sizeof(item->sourceId), "%s", source->id);
  snprintf(
      item->id, sizeof(item->id), "%s\n%s", uid ? uid : "", rid ? rid : "");
  g_free(rid);
  ECalComponentText *summary = e_cal_component_get_summary(component);
  copyText(item->title,
           sizeof(item->title),
           summary ? e_cal_component_text_get_value(summary) : NULL);
  e_cal_component_text_free(summary);
  snprintf(item->sourceName, sizeof(item->sourceName), "%s", source->name);
  snapshot->count++;
  return true;
}

static gboolean appendEventInstance(ICalComponent *ical,
                                    ICalTime *instanceStart,
                                    ICalTime *instanceEnd,
                                    gpointer userData,
                                    GCancellable *cancellable,
                                    GError **error) {
  (void)cancellable;
  (void)error;
  InstanceContext *context = userData;
  ECalComponent *component =
      e_cal_component_new_from_icalcomponent(i_cal_component_clone(ical));
  if (!component)
    return TRUE;
  bool allDay = i_cal_time_is_date(instanceStart);
  appendComponent(context->snapshot,
                  context->source,
                  component,
                  i_cal_time_as_timet(instanceStart),
                  i_cal_time_as_timet(instanceEnd),
                  allDay);
  g_object_unref(component);
  return context->snapshot->count < AGENDA_ENTRY_MAX;
}

static void addTasks(AgendaSnapshot *snapshot,
                     OpenSource *source,
                     GCancellable *cancellable) {
  GSList *components = NULL;
  GError *error = NULL;
  if (!e_cal_client_get_object_list_as_comps_sync(
          source->client, "#t", &components, cancellable, &error)) {
    logMessage("WARNING",
               "agenda source %s object query failed (%s)",
               source->id,
               error ? g_quark_to_string(error->domain) : "unknown");
    g_clear_error(&error);
    return;
  }
  for (GSList *node = components; node; node = node->next) {
    ECalComponent *component = node->data;
    ECalComponentDateTime *due = e_cal_component_get_due(component);
    ICalTime *value = due ? e_cal_component_datetime_get_value(due) : NULL;
    time_t dueTime = value ? i_cal_time_as_timet(value) : 0;
    bool allDay = value && i_cal_time_is_date(value);
    appendComponent(snapshot, source, component, dueTime, dueTime, allDay);
    e_cal_component_datetime_free(due);
  }
  e_client_util_free_object_slist(components);
}

static void
markRefresh(ECalClientView *view, const GSList *objects, gpointer userData) {
  (void)view;
  (void)objects;
  OpenSource *source = userData;
  if (source->viewReady)
    source->provider->refreshRequested = true;
}

static void
markViewReady(ECalClientView *view, const GError *error, gpointer userData) {
  (void)view;
  (void)error;
  OpenSource *source = userData;
  source->viewReady = true;
}

static void closeSources(AgendaProvider *provider) {
  for (size_t index = 0; index < provider->sourceCount; index++) {
    if (provider->sources[index].view) {
      e_cal_client_view_stop(provider->sources[index].view, NULL);
      g_object_unref(provider->sources[index].view);
    }
    if (provider->sources[index].client)
      g_object_unref(provider->sources[index].client);
  }
  memset(provider->sources, 0, sizeof(provider->sources));
  provider->sourceCount = 0;
}

static void notifyMain(AgendaProvider *provider) {
  const unsigned char BYTE = 1;
  if (write(provider->notification[1], &BYTE, 1) < 0 && errno != EAGAIN)
    logMessage("WARNING", "agenda notification failed: %s", strerror(errno));
}

static void publishSnapshot(AgendaProvider *provider,
                            const AgendaSnapshot *newSnapshot,
                            const AgendaProviderStatus *status) {
  AgendaSnapshot snapshot = *newSnapshot;
  snapshot.selectedSourceCount = status->selectedSources;
  snapshot.reachableSourceCount = status->reachableSources;
  snapshot.failedSourceCount = status->failedSources;
  snapshot.initialized = status->initialized;
  pthread_mutex_lock(&provider->mutex);
  provider->snapshot = snapshot;
  provider->status = *status;
  pthread_mutex_unlock(&provider->mutex);
  notifyMain(provider);
}

static size_t warnUnknownExplicitSources(const PanelConfig *config,
                                         GList *calendarSources,
                                         GList *taskSources) {
  size_t missing = 0;
  const struct {
    AgendaItemType type;
    GList *sources;
    size_t count;
    const char (*ids)[PANEL_AGENDA_SOURCE_ID_MAX];
  } GROUPS[] = {{AGENDA_ITEM_EVENT,
                 calendarSources,
                 config->agendaCalendarSourceCount,
                 config->agendaCalendarSources},
                {AGENDA_ITEM_TASK,
                 taskSources,
                 config->agendaTaskSourceCount,
                 config->agendaTaskSources}};
  for (size_t group = 0; group < 2; group++) {
    AgendaSourceMode mode = GROUPS[group].type == AGENDA_ITEM_EVENT
                                ? config->agendaCalendarSourceMode
                                : config->agendaTaskSourceMode;
    if (mode != AGENDA_SOURCES_EXPLICIT)
      continue;
    for (size_t idIndex = 0; idIndex < GROUPS[group].count; idIndex++) {
      bool found = false;
      for (GList *node = GROUPS[group].sources; node; node = node->next)
        if (!strcmp(e_source_get_uid(node->data), GROUPS[group].ids[idIndex])) {
          found = true;
          break;
        }
      if (!found)
        logMessage("WARNING",
                   "agenda source %s is not available",
                   GROUPS[group].ids[idIndex]);
      if (!found)
        missing++;
    }
  }
  return missing;
}

static void *sourceTimeoutThread(void *data) {
  SourceTimeout *timeout = data;
  for (unsigned attempt = 0; attempt < 200; attempt++) {
    if (atomic_load(&timeout->finished))
      return NULL;
    if (atomic_load(&timeout->provider->stopping))
      break;
    g_usleep(50000);
  }
  if (!atomic_load(&timeout->finished))
    g_cancellable_cancel(timeout->cancellable);
  return NULL;
}

static GCancellable *startSourceTimeout(AgendaProvider *provider,
                                        SourceTimeout *timeout) {
  memset(timeout, 0, sizeof(*timeout));
  timeout->provider = provider;
  timeout->cancellable = g_cancellable_new();
  if (!pthread_create(&timeout->thread, NULL, sourceTimeoutThread, timeout))
    timeout->started = true;
  else
    logMessage("WARNING", "agenda source timeout could not be started");
  return timeout->cancellable;
}

static void finishSourceTimeout(SourceTimeout *timeout) {
  atomic_store(&timeout->finished, true);
  if (timeout->started)
    pthread_join(timeout->thread, NULL);
  g_object_unref(timeout->cancellable);
}

static void refreshSnapshot(AgendaProvider *provider) {
  closeSources(provider);
  AgendaSnapshot snapshot = {0};
  AgendaProviderStatus status = {.initialized = true};
  GError *error = NULL;
  ESourceRegistry *registry =
      e_source_registry_new_sync(provider->cancellable, &error);
  if (!registry) {
    logMessage("WARNING",
               "agenda registry unavailable (%s)",
               error ? g_quark_to_string(error->domain) : "unknown");
    g_clear_error(&error);
    publishSnapshot(provider, &snapshot, &status);
    return;
  }
  GList *calendarSources =
      e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_CALENDAR);
  GList *taskSources =
      e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_TASK_LIST);
  status.failedSources = warnUnknownExplicitSources(
      &provider->config, calendarSources, taskSources);
  const struct {
    GList *sources;
    AgendaItemType type;
    ECalClientSourceType clientType;
  } GROUPS[] = {
      {calendarSources, AGENDA_ITEM_EVENT, E_CAL_CLIENT_SOURCE_TYPE_EVENTS},
      {taskSources, AGENDA_ITEM_TASK, E_CAL_CLIENT_SOURCE_TYPE_TASKS}};
  for (size_t group = 0; group < 2; group++) {
    size_t configured =
        configuredSourceCount(&provider->config, GROUPS[group].type);
    status.selectedSources += configured == SIZE_MAX
                                  ? g_list_length(GROUPS[group].sources)
                                  : configured;
  }
  for (size_t group = 0; group < 2; group++) {
    for (GList *node = GROUPS[group].sources; node; node = node->next) {
      ESource *source = node->data;
      const char *id = e_source_get_uid(source);
      if (!sourceSelected(&provider->config, GROUPS[group].type, id))
        continue;
      size_t snapshotStart = snapshot.count;
      SourceTimeout timeout;
      GCancellable *sourceCancellable = startSourceTimeout(provider, &timeout);
      EClient *base = e_cal_client_connect_sync(
          source, GROUPS[group].clientType, 10, sourceCancellable, &error);
      if (!base) {
        finishSourceTimeout(&timeout);
        status.failedSources++;
        logMessage("WARNING",
                   "agenda source %s connection failed (%s)",
                   id,
                   error ? g_quark_to_string(error->domain) : "unknown");
        g_clear_error(&error);
        publishSnapshot(provider, &snapshot, &status);
        continue;
      }
      if (provider->sourceCount >= AGENDA_PROVIDER_SOURCE_MAX) {
        g_object_unref(base);
        finishSourceTimeout(&timeout);
        break;
      }
      OpenSource *opened = &provider->sources[provider->sourceCount++];
      opened->provider = provider;
      opened->client = E_CAL_CLIENT(base);
      opened->type = GROUPS[group].type;
      snprintf(opened->id, sizeof(opened->id), "%s", id);
      copyText(opened->name,
               sizeof(opened->name),
               e_source_get_display_name(source));
      status.reachableSources++;
      if (opened->type == AGENDA_ITEM_EVENT) {
        InstanceContext context = {provider, &snapshot, opened};
        e_cal_client_generate_instances_sync(
            opened->client,
            time(NULL),
            localRangeEnd(provider->config.agendaDays),
            sourceCancellable,
            appendEventInstance,
            &context);
      } else {
        addTasks(&snapshot, opened, sourceCancellable);
      }
      if (e_cal_client_get_view_sync(
              opened->client, "#t", &opened->view, sourceCancellable, &error)) {
        g_signal_connect(
            opened->view, "objects-added", G_CALLBACK(markRefresh), opened);
        g_signal_connect(
            opened->view, "objects-modified", G_CALLBACK(markRefresh), opened);
        g_signal_connect(
            opened->view, "objects-removed", G_CALLBACK(markRefresh), opened);
        g_signal_connect(
            opened->view, "complete", G_CALLBACK(markViewReady), opened);
        e_cal_client_view_start(opened->view, NULL);
      } else {
        logMessage("WARNING",
                   "agenda source %s change view failed (%s)",
                   id,
                   error ? g_quark_to_string(error->domain) : "unknown");
        g_clear_error(&error);
      }
      bool sourceTimedOut = g_cancellable_is_cancelled(sourceCancellable);
      finishSourceTimeout(&timeout);
      if (sourceTimedOut) {
        if (opened->view) {
          e_cal_client_view_stop(opened->view, NULL);
          g_object_unref(opened->view);
        }
        g_object_unref(opened->client);
        memset(opened, 0, sizeof(*opened));
        provider->sourceCount--;
        snapshot.count = snapshotStart;
        status.reachableSources--;
        status.failedSources++;
        logMessage("WARNING", "agenda source %s initialization timed out", id);
      }
      publishSnapshot(provider, &snapshot, &status);
    }
  }
  g_list_free_full(calendarSources, g_object_unref);
  g_list_free_full(taskSources, g_object_unref);
  g_object_unref(registry);
  publishSnapshot(provider, &snapshot, &status);
}

static void *providerThread(void *data) {
  AgendaProvider *provider = data;
  provider->context = g_main_context_new();
  g_main_context_push_thread_default(provider->context);
  time_t nextRefresh = 0;
  int64_t dirtySince = 0;
  while (!atomic_load(&provider->stopping)) {
    while (g_main_context_iteration(provider->context, FALSE)) {
    }
    int64_t nowMonotonic = g_get_monotonic_time();
    if (provider->refreshRequested && dirtySince == 0)
      dirtySince = nowMonotonic;
    bool debounceElapsed =
        dirtySince > 0 && nowMonotonic - dirtySince >= 200000;
    time_t now = time(NULL);
    if (now >= nextRefresh || debounceElapsed) {
      provider->refreshRequested = false;
      dirtySince = 0;
      refreshSnapshot(provider);
      nextRefresh = now + (time_t)provider->config.agendaRefreshInterval;
    }
    g_usleep(50000);
  }
  g_cancellable_cancel(provider->cancellable);
  closeSources(provider);
  g_main_context_pop_thread_default(provider->context);
  g_main_context_unref(provider->context);
  provider->context = NULL;
  return NULL;
}

bool agendaProviderEdsCompiled(void) {
  return true;
}

static void printSources(FILE *output,
                         ESourceRegistry *registry,
                         const char *extension,
                         const char *type) {
  GList *sources = e_source_registry_list_sources(registry, extension);
  for (GList *node = sources; node; node = node->next) {
    ESource *source = node->data;
    char displayName[AGENDA_SOURCE_NAME_MAX];
    copyText(
        displayName, sizeof(displayName), e_source_get_display_name(source));
    fprintf(output,
            "%s\t%s\t%s\t%s\n",
            type,
            e_source_get_enabled(source) ? "enabled" : "disabled",
            e_source_get_uid(source),
            displayName);
  }
  g_list_free_full(sources, g_object_unref);
}

int agendaProviderListSources(FILE *output, FILE *errorOutput) {
  GError *error = NULL;
  ESourceRegistry *registry = e_source_registry_new_sync(NULL, &error);
  if (!registry) {
    fprintf(errorOutput,
            "sliverbar: cannot access Evolution Data Server (%s)\n",
            error ? g_quark_to_string(error->domain) : "unknown");
    g_clear_error(&error);
    return 1;
  }
  printSources(output, registry, E_SOURCE_EXTENSION_CALENDAR, "calendar");
  printSources(output, registry, E_SOURCE_EXTENSION_TASK_LIST, "tasks");
  g_object_unref(registry);
  return 0;
}

AgendaProvider *agendaProviderCreate(const PanelConfig *config) {
  if (!config || strcmp(config->agendaProvider, "eds") != 0)
    return NULL;
  AgendaProvider *provider = calloc(1, sizeof(*provider));
  if (!provider)
    return NULL;
  provider->config = *config;
  provider->notification[0] = -1;
  provider->notification[1] = -1;
  if (pthread_mutex_init(&provider->mutex, NULL)) {
    free(provider);
    return NULL;
  }
  if (pipe2(provider->notification, O_CLOEXEC | O_NONBLOCK)) {
    pthread_mutex_destroy(&provider->mutex);
    free(provider);
    return NULL;
  }
  provider->cancellable = g_cancellable_new();
  return provider;
}

int agendaProviderStart(AgendaProvider *provider) {
  if (!provider || provider->started)
    return -1;
  if (pthread_create(&provider->thread, NULL, providerThread, provider))
    return -1;
  provider->started = true;
  return 0;
}

int agendaProviderPollFd(const AgendaProvider *provider) {
  return provider ? provider->notification[0] : -1;
}

bool agendaProviderRead(AgendaProvider *provider,
                        AgendaSnapshot *snapshot,
                        AgendaProviderStatus *status) {
  if (!provider)
    return false;
  unsigned char bytes[64];
  while (read(provider->notification[0], bytes, sizeof(bytes)) > 0) {
  }
  pthread_mutex_lock(&provider->mutex);
  if (snapshot)
    *snapshot = provider->snapshot;
  if (status)
    *status = provider->status;
  pthread_mutex_unlock(&provider->mutex);
  return true;
}

void agendaProviderDestroy(AgendaProvider *provider) {
  if (!provider)
    return;
  atomic_store(&provider->stopping, true);
  g_cancellable_cancel(provider->cancellable);
  if (provider->started)
    pthread_join(provider->thread, NULL);
  g_object_unref(provider->cancellable);
  close(provider->notification[0]);
  close(provider->notification[1]);
  pthread_mutex_destroy(&provider->mutex);
  free(provider);
}

#else

struct AgendaProvider {
  PanelConfig config;
};

bool agendaProviderEdsCompiled(void) {
  return false;
}

int agendaProviderListSources(FILE *output, FILE *errorOutput) {
  (void)output;
  fputs("sliverbar: Evolution Data Server support is not compiled in\n",
        errorOutput);
  return 1;
}

AgendaProvider *agendaProviderCreate(const PanelConfig *config) {
  (void)config;
  return NULL;
}

int agendaProviderStart(AgendaProvider *provider) {
  (void)provider;
  return -1;
}

int agendaProviderPollFd(const AgendaProvider *provider) {
  (void)provider;
  return -1;
}

bool agendaProviderRead(AgendaProvider *provider,
                        AgendaSnapshot *snapshot,
                        AgendaProviderStatus *status) {
  (void)provider;
  (void)snapshot;
  (void)status;
  return false;
}

void agendaProviderDestroy(AgendaProvider *provider) {
  free(provider);
}

#endif
