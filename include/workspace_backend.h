#ifndef SLIVERBAR_WORKSPACE_BACKEND_H
#define SLIVERBAR_WORKSPACE_BACKEND_H

#include "panel.h"

#ifdef HAVE_NATIVE_PANEL

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <xcb/xcb.h>

typedef struct WorkspaceBackend WorkspaceBackend;

WorkspaceBackend *workspaceBackendCreate(xcb_connection_t *connection,
                                         xcb_window_t root,
                                         const PanelConfig *config);
void workspaceBackendDestroy(WorkspaceBackend *backend);
const char *workspaceBackendName(const WorkspaceBackend *backend);
int workspaceBackendPollFd(const WorkspaceBackend *backend);
pid_t workspaceBackendChildPid(const WorkspaceBackend *backend);
bool workspaceBackendRead(WorkspaceBackend *backend, PanelState *state);
bool workspaceBackendRefresh(WorkspaceBackend *backend, PanelState *state);
bool workspaceBackendChildExited(WorkspaceBackend *backend,
                                 pid_t pid,
                                 int status,
                                 PanelState *state);
int workspaceBackendSwitch(WorkspaceBackend *backend, const char *workspace);

#endif

#endif
