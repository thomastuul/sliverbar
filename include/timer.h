#ifndef SLIVERBAR_TIMER_H
#define SLIVERBAR_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define TIMER_MAX_MINUTES 1440U

typedef enum {
  TIMER_EMPTY,
  TIMER_SET,
  TIMER_RUNNING,
  TIMER_PAUSED,
} TimerStatus;

typedef enum {
  TIMER_TRANSITION_NONE,
  TIMER_TRANSITION_STARTED,
  TIMER_TRANSITION_PAUSED,
  TIMER_TRANSITION_RESUMED,
  TIMER_TRANSITION_EXPIRED,
} TimerTransition;

typedef struct {
  TimerStatus status;
  uint64_t remainingNs;
  uint64_t deadlineNs;
} Timer;

uint64_t timerNowNs(void);
bool timerAdjust(Timer *timer, int minutes);
TimerTransition timerToggle(Timer *timer, uint64_t nowNs);
bool timerUpdate(Timer *timer, uint64_t nowNs);
void timerReset(Timer *timer);
unsigned timerMinutes(const Timer *timer);

#endif
