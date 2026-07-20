#ifndef SLIVERBAR_INHIBITOR_H
#define SLIVERBAR_INHIBITOR_H

#include <stdbool.h>
#include <sys/types.h>

typedef struct Inhibitor Inhibitor;

Inhibitor *inhibitorCreate(const char *executable);
void inhibitorDestroy(Inhibitor *inhibitor);
bool inhibitorAvailable(const Inhibitor *inhibitor);
bool inhibitorActive(const Inhibitor *inhibitor);
const char *inhibitorBackendName(const Inhibitor *inhibitor);
pid_t inhibitorPid(const Inhibitor *inhibitor);
int inhibitorToggle(Inhibitor *inhibitor);
int inhibitorSetActive(Inhibitor *inhibitor, bool active);
bool inhibitorChildExited(Inhibitor *inhibitor, pid_t pid);

#endif
