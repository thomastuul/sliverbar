#include "workspace_backend.h"

#ifdef HAVE_NATIVE_PANEL

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum {
  WORKSPACE_BACKEND_NONE,
  WORKSPACE_BACKEND_EWMH,
  WORKSPACE_BACKEND_BSPWM,
} WorkspaceBackendType;

struct WorkspaceBackend {
  WorkspaceBackendType type;
  xcb_connection_t *connection;
  xcb_window_t root;
  PanelConfig config;
  pid_t childPid;
  int childFd;
  char report[PANEL_TEXT_MAX];
  size_t reportUsed;
  WorkspaceSnapshot snapshot;
  bool hasSnapshot;
  xcb_atom_t netNumberOfDesktops;
  xcb_atom_t netCurrentDesktop;
  xcb_atom_t netDesktopNames;
  xcb_atom_t netClientList;
  xcb_atom_t netWmDesktop;
  xcb_atom_t netWmState;
  xcb_atom_t netWmStateDemandsAttention;
  xcb_atom_t utf8String;
};

static xcb_atom_t internAtom(xcb_connection_t *connection, const char *name) {
  xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
  xcb_intern_atom_reply_t *reply =
      xcb_intern_atom_reply(connection, cookie, NULL);
  xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
  free(reply);
  return result;
}

static xcb_get_property_reply_t *property(xcb_connection_t *connection,
                                          xcb_window_t window,
                                          xcb_atom_t atom,
                                          xcb_atom_t type,
                                          uint32_t length) {
  return xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, window, atom, type, 0, length),
      NULL);
}

static bool cardinal(WorkspaceBackend *backend,
                     xcb_window_t window,
                     xcb_atom_t atom,
                     uint32_t *value) {
  xcb_get_property_reply_t *reply =
      property(backend->connection, window, atom, XCB_ATOM_CARDINAL, 1);
  bool valid = reply && reply->format == 32 &&
               xcb_get_property_value_length(reply) >= (int)sizeof(*value);
  if (valid)
    memcpy(value, xcb_get_property_value(reply), sizeof(*value));
  free(reply);
  return valid;
}

static bool atomListContains(WorkspaceBackend *backend,
                             xcb_window_t window,
                             xcb_atom_t propertyAtom,
                             xcb_atom_t expected) {
  xcb_get_property_reply_t *reply =
      property(backend->connection, window, propertyAtom, XCB_ATOM_ATOM, 1024);
  int length = reply ? xcb_get_property_value_length(reply) : 0;
  const xcb_atom_t *values = reply ? xcb_get_property_value(reply) : NULL;
  bool found = false;
  for (int i = 0; values && i < length / (int)sizeof(*values); i++)
    if (values[i] == expected) {
      found = true;
      break;
    }
  free(reply);
  return found;
}

static bool windowUrgent(WorkspaceBackend *backend, xcb_window_t window) {
  if (atomListContains(backend,
                       window,
                       backend->netWmState,
                       backend->netWmStateDemandsAttention))
    return true;
  xcb_get_property_reply_t *reply = property(backend->connection,
                                             window,
                                             XCB_ATOM_WM_HINTS,
                                             XCB_GET_PROPERTY_TYPE_ANY,
                                             9);
  uint32_t flags = 0;
  bool valid = reply && reply->format == 32 &&
               xcb_get_property_value_length(reply) >= (int)sizeof(flags);
  if (valid)
    memcpy(&flags, xcb_get_property_value(reply), sizeof(flags));
  free(reply);
  return valid && (flags & (1U << 8U));
}

static void desktopNames(WorkspaceBackend *backend,
                         WorkspaceSnapshot *snapshot) {
  xcb_get_property_reply_t *reply = property(backend->connection,
                                             backend->root,
                                             backend->netDesktopNames,
                                             backend->utf8String,
                                             4096);
  const char *value = reply ? xcb_get_property_value(reply) : NULL;
  size_t remaining = reply && xcb_get_property_value_length(reply) > 0
                         ? (size_t)xcb_get_property_value_length(reply)
                         : 0;
  for (size_t i = 0; i < snapshot->count; i++) {
    if (value && remaining > 0) {
      size_t sourceLength = strnlen(value, remaining);
      size_t copyLength = sourceLength;
      if (copyLength >= sizeof(snapshot->names[i]))
        copyLength = sizeof(snapshot->names[i]) - 1;
      memcpy(snapshot->names[i], value, copyLength);
      snapshot->names[i][copyLength] = '\0';
      size_t consumed = sourceLength < remaining ? sourceLength + 1 : remaining;
      value += consumed;
      remaining -= consumed;
    }
    if (!snapshot->names[i][0])
      snprintf(snapshot->names[i], sizeof(snapshot->names[i]), "%zu", i + 1);
  }
  free(reply);
}

static bool ewmhSnapshot(WorkspaceBackend *backend,
                         WorkspaceSnapshot *snapshot) {
  uint32_t count = 0, current = 0;
  if (!cardinal(backend, backend->root, backend->netNumberOfDesktops, &count) ||
      count == 0)
    return false;
  if (count > PANEL_WORKSPACE_MAX)
    count = PANEL_WORKSPACE_MAX;
  cardinal(backend, backend->root, backend->netCurrentDesktop, &current);
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->count = count;
  snapshot->current = current < count ? current : 0;
  desktopNames(backend, snapshot);

  xcb_get_property_reply_t *reply = property(backend->connection,
                                             backend->root,
                                             backend->netClientList,
                                             XCB_ATOM_WINDOW,
                                             16384);
  int length = reply ? xcb_get_property_value_length(reply) : 0;
  const xcb_window_t *windows = reply ? xcb_get_property_value(reply) : NULL;
  for (int i = 0; windows && i < length / (int)sizeof(*windows); i++) {
    uint32_t desktop = 0;
    if (!cardinal(backend, windows[i], backend->netWmDesktop, &desktop))
      continue;
    bool urgent = windowUrgent(backend, windows[i]);
    if (desktop == UINT32_MAX) {
      for (size_t j = 0; j < snapshot->count; j++) {
        snapshot->occupied[j] = true;
        snapshot->urgent[j] = snapshot->urgent[j] || urgent;
      }
    } else if (desktop < snapshot->count) {
      snapshot->occupied[desktop] = true;
      snapshot->urgent[desktop] = snapshot->urgent[desktop] || urgent;
    }
  }
  free(reply);
  return true;
}

static int startBspwm(WorkspaceBackend *backend) {
  int pipeFds[2];
  if (pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK))
    return -1;
  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    return -1;
  }
  if (pid == 0) {
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    dup2(pipeFds[1], STDOUT_FILENO);
    close(pipeFds[0]);
    close(pipeFds[1]);
    char *argv[] = {"bspc", "subscribe", "report", NULL};
    execvp(argv[0], argv);
    _exit(127);
  }
  close(pipeFds[1]);
  backend->childPid = pid;
  backend->childFd = pipeFds[0];
  backend->type = WORKSPACE_BACKEND_BSPWM;
  return 0;
}

static bool bspwmReachable(void) {
  if (!commandExists("bspc"))
    return false;
  char output[256];
  char *argv[] = {"bspc", "query", "-M", NULL};
  return runCapture(argv, output, sizeof(output), 1000) == 0;
}

WorkspaceBackend *workspaceBackendCreate(xcb_connection_t *connection,
                                         xcb_window_t root,
                                         const PanelConfig *config) {
  WorkspaceBackend *backend = calloc(1, sizeof(*backend));
  if (!backend)
    return NULL;
  backend->connection = connection;
  backend->root = root;
  backend->config = *config;
  backend->childFd = -1;
  backend->netNumberOfDesktops =
      internAtom(connection, "_NET_NUMBER_OF_DESKTOPS");
  backend->netCurrentDesktop = internAtom(connection, "_NET_CURRENT_DESKTOP");
  backend->netDesktopNames = internAtom(connection, "_NET_DESKTOP_NAMES");
  backend->netClientList = internAtom(connection, "_NET_CLIENT_LIST");
  backend->netWmDesktop = internAtom(connection, "_NET_WM_DESKTOP");
  backend->netWmState = internAtom(connection, "_NET_WM_STATE");
  backend->netWmStateDemandsAttention =
      internAtom(connection, "_NET_WM_STATE_DEMANDS_ATTENTION");
  backend->utf8String = internAtom(connection, "UTF8_STRING");

  if (!strcmp(config->workspaceBackend, "none")) {
    backend->type = WORKSPACE_BACKEND_NONE;
  } else if ((!strcmp(config->workspaceBackend, "auto") ||
              !strcmp(config->workspaceBackend, "bspwm")) &&
             bspwmReachable() && !startBspwm(backend)) {
    logMessage("INFO", "workspace backend: bspwm");
  } else {
    if (!strcmp(config->workspaceBackend, "bspwm"))
      logMessage("WARNING",
                 "bspwm workspace backend unavailable; falling back to EWMH");
    backend->type = WORKSPACE_BACKEND_EWMH;
    logMessage("INFO", "workspace backend: EWMH");
  }
  return backend;
}

void workspaceBackendDestroy(WorkspaceBackend *backend) {
  if (!backend)
    return;
  if (backend->childPid > 0) {
    kill(backend->childPid, SIGTERM);
    for (int i = 0; i < 20 && waitpid(backend->childPid, NULL, WNOHANG) == 0;
         i++)
      usleep(10000);
    if (waitpid(backend->childPid, NULL, WNOHANG) == 0) {
      kill(backend->childPid, SIGKILL);
      waitpid(backend->childPid, NULL, 0);
    }
  }
  if (backend->childFd >= 0)
    close(backend->childFd);
  free(backend);
}

const char *workspaceBackendName(const WorkspaceBackend *backend) {
  if (!backend || backend->type == WORKSPACE_BACKEND_NONE)
    return "none";
  return backend->type == WORKSPACE_BACKEND_BSPWM ? "bspwm" : "ewmh";
}

int workspaceBackendPollFd(const WorkspaceBackend *backend) {
  return backend && backend->type == WORKSPACE_BACKEND_BSPWM ? backend->childFd
                                                             : -1;
}

pid_t workspaceBackendChildPid(const WorkspaceBackend *backend) {
  return backend && backend->type == WORKSPACE_BACKEND_BSPWM ? backend->childPid
                                                             : 0;
}

bool workspaceBackendRead(WorkspaceBackend *backend, PanelState *state) {
  if (!backend || backend->type != WORKSPACE_BACKEND_BSPWM ||
      backend->childFd < 0)
    return false;
  bool changed = false;
  ssize_t count = read(backend->childFd,
                       backend->report + backend->reportUsed,
                       sizeof(backend->report) - backend->reportUsed - 1);
  if (count > 0) {
    backend->reportUsed += (size_t)count;
    backend->report[backend->reportUsed] = '\0';
    char *line = backend->report;
    char *end = backend->report + backend->reportUsed;
    char *newline;
    while ((newline = memchr(line, '\n', (size_t)(end - line)))) {
      *newline = '\0';
      if (*line) {
        moduleWorkspace(&backend->config, state, line);
        changed = true;
      }
      line = newline + 1;
    }
    backend->reportUsed = (size_t)(end - line);
    memmove(backend->report, line, backend->reportUsed);
    backend->report[backend->reportUsed] = '\0';
  }
  if (backend->reportUsed == sizeof(backend->report) - 1) {
    logMessage("ERROR", "discarding oversized bspwm report");
    backend->reportUsed = 0;
    backend->report[0] = '\0';
  }
  return changed;
}

bool workspaceBackendRefresh(WorkspaceBackend *backend, PanelState *state) {
  if (!backend || backend->type == WORKSPACE_BACKEND_BSPWM)
    return false;
  if (backend->type == WORKSPACE_BACKEND_NONE) {
    bool changed = state->workspace[0] || state->focusedWorkspaceKnown;
    state->workspace[0] = '\0';
    state->focusedWorkspaceKnown = false;
    state->focusedWorkspaceOccupied = false;
    return changed;
  }
  WorkspaceSnapshot snapshot;
  if (!ewmhSnapshot(backend, &snapshot)) {
    bool changed = state->workspace[0] || state->focusedWorkspaceKnown;
    state->workspace[0] = '\0';
    state->focusedWorkspaceKnown = false;
    state->focusedWorkspaceOccupied = false;
    backend->hasSnapshot = false;
    return changed;
  }
  if (backend->hasSnapshot &&
      !memcmp(&backend->snapshot, &snapshot, sizeof(snapshot)))
    return false;
  backend->snapshot = snapshot;
  backend->hasSnapshot = true;
  moduleWorkspaceEwmh(&backend->config, state, &snapshot);
  return true;
}

bool workspaceBackendChildExited(WorkspaceBackend *backend,
                                 pid_t pid,
                                 int status,
                                 PanelState *state) {
  if (!backend || backend->type != WORKSPACE_BACKEND_BSPWM ||
      pid != backend->childPid)
    return false;
  logMessage("WARNING",
             "bspwm workspace backend exited with status %d; using EWMH",
             WIFEXITED(status) ? WEXITSTATUS(status) : -1);
  if (backend->childFd >= 0)
    close(backend->childFd);
  backend->childFd = -1;
  backend->childPid = 0;
  backend->reportUsed = 0;
  backend->type = WORKSPACE_BACKEND_EWMH;
  backend->hasSnapshot = false;
  return workspaceBackendRefresh(backend, state);
}

int workspaceBackendSwitch(WorkspaceBackend *backend, const char *workspace) {
  if (!backend || !workspace)
    return -1;
  if (backend->type == WORKSPACE_BACKEND_BSPWM) {
    char output[64];
    char *argv[] = {"bspc", "desktop", "-f", (char *)workspace, NULL};
    return runCapture(argv, output, sizeof(output), 1000);
  }
  if (backend->type != WORKSPACE_BACKEND_EWMH)
    return -1;
  char *end = NULL;
  errno = 0;
  unsigned long index = strtoul(workspace, &end, 10);
  if (errno || end == workspace || *end || index >= PANEL_WORKSPACE_MAX ||
      (backend->hasSnapshot && index >= backend->snapshot.count))
    return -1;
  xcb_client_message_event_t event = {0};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = backend->root;
  event.type = backend->netCurrentDesktop;
  event.data.data32[0] = (uint32_t)index;
  event.data.data32[1] = XCB_CURRENT_TIME;
  event.data.data32[2] = 2;
  xcb_void_cookie_t cookie = xcb_send_event_checked(
      backend->connection,
      0,
      backend->root,
      XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
      (const char *)&event);
  xcb_generic_error_t *error = xcb_request_check(backend->connection, cookie);
  int result = error ? -1 : 0;
  free(error);
  xcb_flush(backend->connection);
  return result;
}

#endif
