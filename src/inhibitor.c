#include "inhibitor.h"

#include "panel.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct Inhibitor {
  char executable[PANEL_PATH_MAX];
  pid_t pid;
  bool available;
};

Inhibitor *inhibitorCreate(const char *executable) {
  Inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
  if (!inhibitor)
    return NULL;
  inhibitor->available =
      executable && *executable && commandExists("systemd-inhibit");
  if (executable)
    snprintf(
        inhibitor->executable, sizeof(inhibitor->executable), "%s", executable);
  return inhibitor;
}

static void stop(Inhibitor *inhibitor) {
  if (!inhibitor || inhibitor->pid <= 0)
    return;
  kill(inhibitor->pid, SIGTERM);
  for (int i = 0; i < 20 && waitpid(inhibitor->pid, NULL, WNOHANG) == 0; i++)
    usleep(10000);
  if (waitpid(inhibitor->pid, NULL, WNOHANG) == 0) {
    kill(inhibitor->pid, SIGKILL);
    waitpid(inhibitor->pid, NULL, 0);
  }
  inhibitor->pid = 0;
}

void inhibitorDestroy(Inhibitor *inhibitor) {
  if (!inhibitor)
    return;
  stop(inhibitor);
  free(inhibitor);
}

bool inhibitorAvailable(const Inhibitor *inhibitor) {
  return inhibitor && inhibitor->available;
}

bool inhibitorActive(const Inhibitor *inhibitor) {
  return inhibitor && inhibitor->pid > 0 && kill(inhibitor->pid, 0) == 0;
}

const char *inhibitorBackendName(const Inhibitor *inhibitor) {
  return inhibitorAvailable(inhibitor) ? "systemd-inhibit" : "none";
}

pid_t inhibitorPid(const Inhibitor *inhibitor) {
  return inhibitor ? inhibitor->pid : 0;
}

static int start(Inhibitor *inhibitor) {
  if (!inhibitorAvailable(inhibitor) || inhibitor->pid > 0)
    return -1;
  pid_t pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    char *argv[] = {"systemd-inhibit",
                    "--what=sleep",
                    "--who=Sliverbar",
                    "--why=User requested sleep inhibition",
                    "--mode=block",
                    inhibitor->executable,
                    "--inhibit-holder",
                    NULL};
    execvp(argv[0], argv);
    _exit(127);
  }
  inhibitor->pid = pid;
  return 0;
}

int inhibitorToggle(Inhibitor *inhibitor) {
  if (!inhibitorAvailable(inhibitor))
    return -1;
  if (inhibitorActive(inhibitor)) {
    stop(inhibitor);
    return 0;
  }
  inhibitor->pid = 0;
  return start(inhibitor);
}

int inhibitorSetActive(Inhibitor *inhibitor, bool active) {
  if (!inhibitorAvailable(inhibitor))
    return -1;
  if (active == inhibitorActive(inhibitor))
    return 0;
  return inhibitorToggle(inhibitor);
}

bool inhibitorChildExited(Inhibitor *inhibitor, pid_t pid) {
  if (!inhibitor || pid <= 0 || pid != inhibitor->pid)
    return false;
  inhibitor->pid = 0;
  return true;
}
