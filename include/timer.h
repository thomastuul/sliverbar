#ifndef SLIVERBAR_TIMER_H
#define SLIVERBAR_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define TIMER_MAX_MINUTES 1440U
#define TIMER_ANIMATION_FRAMES 8U

typedef enum {
  TIMER_EMPTY,
  TIMER_SET,
  TIMER_RUNNING,
  TIMER_PAUSED,
} TimerStatus;

typedef enum {
  TIMER_DISPLAY_EMPTY,
  TIMER_DISPLAY_SET,
  TIMER_DISPLAY_RUNNING,
  TIMER_DISPLAY_PAUSED,
  TIMER_DISPLAY_EXPIRED,
  TIMER_DISPLAY_RESET,
} TimerDisplay;

typedef enum {
  TIMER_FEEDBACK_NONE,
  TIMER_FEEDBACK_CANCEL,
  TIMER_FEEDBACK_TIMEOUT,
} TimerFeedbackAction;

typedef enum {
  TIMER_TRANSITION_NONE,
  TIMER_TRANSITION_STARTED,
  TIMER_TRANSITION_PAUSED,
  TIMER_TRANSITION_RESUMED,
  TIMER_TRANSITION_EXPIRED,
} TimerTransition;

typedef struct {
  TimerStatus status;
  TimerDisplay feedback;
  uint64_t remainingNs;
  uint64_t deadlineNs;
  uint64_t animationStartNs;
} Timer;

uint64_t timerNowNs(void);
bool timerAdjust(Timer *timer, int minutes);
TimerTransition timerToggle(Timer *timer, uint64_t nowNs);
bool timerUpdate(Timer *timer, uint64_t nowNs);
void timerReset(Timer *timer);
bool timerResetWithFeedback(Timer *timer);
void timerShowExpired(Timer *timer);
void timerClearFeedback(Timer *timer);
TimerDisplay timerDisplay(const Timer *timer);
unsigned timerAnimationFrame(const Timer *timer, uint64_t nowNs);
TimerFeedbackAction timerSoundFinished(Timer *timer, bool succeeded);
int timerFeedbackTimeoutSet(int timerFd, bool enabled);
int timerAnimationTimeoutSet(int timerFd, bool enabled);
unsigned timerMinutes(const Timer *timer);

#endif
