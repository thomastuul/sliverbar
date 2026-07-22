#include "timer.h"

#include <sys/timerfd.h>
#include <time.h>

#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#define NANOSECONDS_PER_MINUTE (UINT64_C(60) * NANOSECONDS_PER_SECOND)

static void timerClearCountdown(Timer *timer) {
  timer->status = TIMER_EMPTY;
  timer->remainingNs = 0;
  timer->deadlineNs = 0;
}

uint64_t timerNowNs(void) {
  struct timespec now = {0};
  if (clock_gettime(CLOCK_MONOTONIC, &now))
    return 0;
  return (uint64_t)now.tv_sec * NANOSECONDS_PER_SECOND + (uint64_t)now.tv_nsec;
}

unsigned timerMinutes(const Timer *timer) {
  if (!timer || !timer->remainingNs)
    return 0;
  return (unsigned)((timer->remainingNs + NANOSECONDS_PER_MINUTE - 1) /
                    NANOSECONDS_PER_MINUTE);
}

bool timerAdjust(Timer *timer, int minutes) {
  if (!timer || (timer->status != TIMER_EMPTY && timer->status != TIMER_SET) ||
      (minutes != 1 && minutes != -1))
    return false;
  unsigned current = timerMinutes(timer);
  if ((minutes > 0 && current >= TIMER_MAX_MINUTES) ||
      (minutes < 0 && current == 0))
    return false;
  current = minutes > 0 ? current + 1 : current - 1;
  timer->remainingNs = (uint64_t)current * NANOSECONDS_PER_MINUTE;
  timer->deadlineNs = 0;
  timer->status = current ? TIMER_SET : TIMER_EMPTY;
  timer->feedback = TIMER_DISPLAY_EMPTY;
  return true;
}

TimerTransition timerToggle(Timer *timer, uint64_t nowNs) {
  if (!timer || !timer->remainingNs)
    return TIMER_TRANSITION_NONE;
  if (timer->status == TIMER_SET) {
    timer->deadlineNs = nowNs + timer->remainingNs;
    timer->status = TIMER_RUNNING;
    return TIMER_TRANSITION_STARTED;
  }
  if (timer->status == TIMER_RUNNING) {
    if (nowNs >= timer->deadlineNs) {
      timerClearCountdown(timer);
      return TIMER_TRANSITION_EXPIRED;
    }
    timer->remainingNs = timer->deadlineNs - nowNs;
    timer->deadlineNs = 0;
    timer->status = TIMER_PAUSED;
    return TIMER_TRANSITION_PAUSED;
  }
  if (timer->status == TIMER_PAUSED) {
    timer->deadlineNs = nowNs + timer->remainingNs;
    timer->status = TIMER_RUNNING;
    return TIMER_TRANSITION_RESUMED;
  }
  return TIMER_TRANSITION_NONE;
}

bool timerUpdate(Timer *timer, uint64_t nowNs) {
  if (!timer || timer->status != TIMER_RUNNING)
    return false;
  if (nowNs >= timer->deadlineNs) {
    timerClearCountdown(timer);
    return true;
  }
  timer->remainingNs = timer->deadlineNs - nowNs;
  return false;
}

void timerReset(Timer *timer) {
  if (!timer)
    return;
  timerClearCountdown(timer);
  timer->feedback = TIMER_DISPLAY_EMPTY;
}

bool timerResetWithFeedback(Timer *timer) {
  if (!timer || timer->status == TIMER_EMPTY)
    return false;
  timerClearCountdown(timer);
  timer->feedback = TIMER_DISPLAY_RESET;
  return true;
}

void timerShowExpired(Timer *timer) {
  if (!timer)
    return;
  timerClearCountdown(timer);
  timer->feedback = TIMER_DISPLAY_EXPIRED;
}

void timerClearFeedback(Timer *timer) {
  if (timer)
    timer->feedback = TIMER_DISPLAY_EMPTY;
}

TimerDisplay timerDisplay(const Timer *timer) {
  if (!timer)
    return TIMER_DISPLAY_EMPTY;
  if (timer->status != TIMER_EMPTY)
    return TIMER_DISPLAY_ACTIVE;
  return timer->feedback;
}

TimerFeedbackAction timerSoundFinished(Timer *timer, bool succeeded) {
  if (!timer || timer->feedback != TIMER_DISPLAY_EXPIRED)
    return TIMER_FEEDBACK_NONE;
  if (!succeeded)
    return TIMER_FEEDBACK_TIMEOUT;
  timerClearFeedback(timer);
  return TIMER_FEEDBACK_CANCEL;
}

int timerFeedbackTimeoutSet(int timerFd, bool enabled) {
  struct itimerspec timeout = {0};
  if (enabled) {
    timeout.it_value.tv_sec = 1;
    timeout.it_value.tv_nsec = 500000000;
  }
  return timerfd_settime(timerFd, 0, &timeout, NULL);
}
