#ifndef SLIVERBAR_AGENDA_H
#define SLIVERBAR_AGENDA_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define AGENDA_ENTRY_MAX 2048
#define AGENDA_VISIBLE_MAX 20
#define AGENDA_ID_MAX 256
#define AGENDA_TITLE_MAX 256
#define AGENDA_SOURCE_NAME_MAX 128

typedef enum {
  AGENDA_ITEM_EVENT,
  AGENDA_ITEM_TASK,
} AgendaItemType;

typedef struct {
  AgendaItemType type;
  char id[AGENDA_ID_MAX];
  char sourceId[AGENDA_ID_MAX];
  char sourceName[AGENDA_SOURCE_NAME_MAX];
  char title[AGENDA_TITLE_MAX];
  time_t start;
  time_t end;
  time_t due;
  bool hasStart;
  bool hasEnd;
  bool hasDue;
  bool dateOnly;
  bool cancelled;
  bool completed;
} AgendaItem;

typedef struct {
  AgendaItem items[AGENDA_ENTRY_MAX];
  size_t count;
  size_t selectedSourceCount;
  size_t reachableSourceCount;
  size_t failedSourceCount;
  bool initialized;
} AgendaSnapshot;

typedef struct {
  AgendaItem item;
  char when[64];
  bool ongoing;
  bool overdue;
} AgendaDisplayItem;

typedef struct {
  AgendaDisplayItem items[AGENDA_VISIBLE_MAX];
  size_t count;
  size_t hiddenEvents;
  size_t hiddenTasks;
  bool initialized;
  bool available;
} AgendaView;

void agendaBuildView(const AgendaSnapshot *snapshot,
                     time_t now,
                     unsigned days,
                     unsigned maxItems,
                     unsigned maxUndatedTasks,
                     bool german,
                     AgendaView *view);

#endif
