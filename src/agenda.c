#include "agenda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const AgendaItem *item;
  time_t order;
  int priority;
  bool ongoing;
  bool overdue;
} Candidate;

static time_t startOfLocalDay(time_t value) {
  struct tm local;
  localtime_r(&value, &local);
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  return mktime(&local);
}

static time_t addLocalDays(time_t value, unsigned days) {
  struct tm local;
  localtime_r(&value, &local);
  local.tm_mday += (int)days;
  local.tm_isdst = -1;
  return mktime(&local);
}

static int localDayDifference(time_t dayStart, time_t value) {
  struct tm base, target;
  localtime_r(&dayStart, &base);
  localtime_r(&value, &target);
  base.tm_hour = 12;
  target.tm_hour = 12;
  base.tm_min = target.tm_min = 0;
  base.tm_sec = target.tm_sec = 0;
  base.tm_isdst = target.tm_isdst = -1;
  double difference = difftime(mktime(&target), mktime(&base));
  return (int)(difference / (24.0 * 60.0 * 60.0));
}

static bool itemOverdue(const AgendaItem *item, time_t now, time_t todayStart) {
  if (item->type != AGENDA_ITEM_TASK || !item->hasDue)
    return false;
  return item->dateOnly ? item->due < todayStart : item->due < now;
}

static bool itemOngoing(const AgendaItem *item, time_t now) {
  return item->type == AGENDA_ITEM_EVENT && item->hasStart && item->hasEnd &&
         item->start <= now && item->end > now;
}

static bool itemVisible(const AgendaItem *item,
                        time_t now,
                        time_t todayStart,
                        time_t rangeEnd) {
  if (item->cancelled || item->completed)
    return false;
  if (item->type == AGENDA_ITEM_EVENT) {
    if (!item->hasStart)
      return false;
    time_t end = item->hasEnd ? item->end : item->start;
    return end > now && item->start < rangeEnd;
  }
  if (!item->hasDue)
    return true;
  return itemOverdue(item, now, todayStart) || item->due < rangeEnd;
}

static Candidate
candidate(const AgendaItem *item, time_t now, time_t todayStart) {
  Candidate result = {.item = item};
  result.overdue = itemOverdue(item, now, todayStart);
  result.ongoing = itemOngoing(item, now);
  if (result.overdue) {
    result.priority = 0;
    result.order = item->due;
  } else if (result.ongoing) {
    result.priority = 1;
    result.order = item->hasEnd ? item->end : item->start;
  } else if (item->type == AGENDA_ITEM_TASK && item->hasDue &&
             item->due < addLocalDays(todayStart, 1)) {
    result.priority = 2;
    result.order = item->due;
  } else if (item->type == AGENDA_ITEM_EVENT &&
             item->start < addLocalDays(todayStart, 1)) {
    result.priority = 3;
    result.order = item->dateOnly ? todayStart : item->start;
  } else if (item->type == AGENDA_ITEM_TASK && !item->hasDue) {
    result.priority = 5;
    result.order = 0;
  } else {
    result.priority = 4;
    result.order = item->type == AGENDA_ITEM_EVENT ? item->start : item->due;
  }
  return result;
}

static int compareCandidates(const void *left, const void *right) {
  const Candidate *a = left;
  const Candidate *b = right;
  if (a->priority != b->priority)
    return a->priority < b->priority ? -1 : 1;
  if (a->order != b->order)
    return a->order < b->order ? -1 : 1;
  if (a->item->dateOnly != b->item->dateOnly)
    return a->item->dateOnly ? -1 : 1;
  int title = strcmp(a->item->title, b->item->title);
  return title ? title : strcmp(a->item->id, b->item->id);
}

static void formatDate(time_t value,
                       time_t todayStart,
                       bool includeTime,
                       bool german,
                       char *output,
                       size_t size) {
  int difference = localDayDifference(todayStart, value);
  struct tm local;
  localtime_r(&value, &local);
  if (difference == 0) {
    if (includeTime)
      strftime(output, size, german ? "Heute %H:%M" : "Today %H:%M", &local);
    else
      snprintf(output, size, "%s", german ? "Heute" : "Today");
    return;
  }
  if (difference == 1) {
    if (includeTime)
      strftime(
          output, size, german ? "Morgen %H:%M" : "Tomorrow %H:%M", &local);
    else
      snprintf(output, size, "%s", german ? "Morgen" : "Tomorrow");
    return;
  }
  strftime(output,
           size,
           includeTime ? (german ? "%a %d.%m. %H:%M" : "%a %m/%d %H:%M")
                       : (german ? "%a %d.%m." : "%a %m/%d"),
           &local);
}

static void formatWhen(const Candidate *candidate,
                       time_t now,
                       time_t todayStart,
                       bool german,
                       char *output,
                       size_t size) {
  const AgendaItem *item = candidate->item;
  if (candidate->overdue) {
    char date[40];
    formatDate(
        item->due, todayStart, !item->dateOnly, german, date, sizeof(date));
    snprintf(output, size, german ? "Ueberfaellig %s" : "Overdue %s", date);
    return;
  }
  if (candidate->ongoing) {
    if (item->dateOnly) {
      char end[40];
      formatDate(item->end, todayStart, false, german, end, sizeof(end));
      snprintf(output, size, german ? "Laufend bis %s" : "Ongoing to %s", end);
    } else {
      struct tm local;
      localtime_r(&item->end, &local);
      strftime(output,
               size,
               german ? "Laufend bis %H:%M" : "Ongoing to %H:%M",
               &local);
    }
    return;
  }
  time_t value = item->type == AGENDA_ITEM_EVENT ? item->start : item->due;
  if (item->type == AGENDA_ITEM_TASK && !item->hasDue) {
    snprintf(output, size, "%s", german ? "Ohne Termin" : "No due date");
    return;
  }
  formatDate(value, todayStart, !item->dateOnly, german, output, size);
  if (item->type == AGENDA_ITEM_EVENT && item->dateOnly) {
    size_t used = strlen(output);
    snprintf(output + used,
             size > used ? size - used : 0,
             "%s",
             german ? " ganztags" : " all day");
  }
  (void)now;
}

static bool candidateSelected(const Candidate *candidate,
                              const Candidate *selected,
                              size_t selectedCount) {
  for (size_t i = 0; i < selectedCount; i++)
    if (selected[i].item == candidate->item)
      return true;
  return false;
}

void agendaBuildView(const AgendaSnapshot *snapshot,
                     time_t now,
                     unsigned days,
                     unsigned maxItems,
                     unsigned maxUndatedTasks,
                     bool german,
                     AgendaView *view) {
  memset(view, 0, sizeof(*view));
  if (!snapshot)
    return;
  view->initialized = snapshot->initialized;
  view->available = snapshot->initialized && snapshot->reachableSourceCount > 0;
  if (!view->available)
    return;
  if (days == 0)
    days = 1;
  if (maxItems > AGENDA_VISIBLE_MAX)
    maxItems = AGENDA_VISIBLE_MAX;
  time_t todayStart = startOfLocalDay(now);
  time_t rangeEnd = addLocalDays(todayStart, days);
  Candidate candidates[AGENDA_ENTRY_MAX];
  size_t candidateCount = 0;
  size_t undatedCount = 0;
  size_t suppressedUndated = 0;
  for (size_t i = 0; i < snapshot->count && candidateCount < AGENDA_ENTRY_MAX;
       i++) {
    const AgendaItem *item = &snapshot->items[i];
    if (!itemVisible(item, now, todayStart, rangeEnd))
      continue;
    if (item->type == AGENDA_ITEM_TASK && !item->hasDue &&
        undatedCount++ >= maxUndatedTasks) {
      suppressedUndated++;
      continue;
    }
    candidates[candidateCount++] = candidate(item, now, todayStart);
  }
  qsort(candidates, candidateCount, sizeof(*candidates), compareCandidates);

  Candidate selected[AGENDA_VISIBLE_MAX];
  size_t selectedCount = 0;
  size_t eventQuota = maxItems / 2;
  size_t taskQuota = maxItems / 2;
  size_t events = 0, tasks = 0;
  for (size_t i = 0; i < candidateCount && selectedCount < maxItems; i++) {
    bool event = candidates[i].item->type == AGENDA_ITEM_EVENT;
    if ((event && events >= eventQuota) || (!event && tasks >= taskQuota))
      continue;
    selected[selectedCount++] = candidates[i];
    if (event)
      events++;
    else
      tasks++;
  }
  for (size_t i = 0; i < candidateCount && selectedCount < maxItems; i++) {
    if (candidateSelected(&candidates[i], selected, selectedCount))
      continue;
    selected[selectedCount++] = candidates[i];
  }
  qsort(selected, selectedCount, sizeof(*selected), compareCandidates);
  for (size_t i = 0; i < selectedCount; i++) {
    view->items[i].item = *selected[i].item;
    view->items[i].ongoing = selected[i].ongoing;
    view->items[i].overdue = selected[i].overdue;
    formatWhen(&selected[i],
               now,
               todayStart,
               german,
               view->items[i].when,
               sizeof(view->items[i].when));
  }
  view->count = selectedCount;
  view->hiddenTasks = suppressedUndated;
  for (size_t i = 0; i < candidateCount; i++) {
    if (candidateSelected(&candidates[i], selected, selectedCount))
      continue;
    if (candidates[i].item->type == AGENDA_ITEM_EVENT)
      view->hiddenEvents++;
    else
      view->hiddenTasks++;
  }
}
