#ifndef SLIVERBAR_CONTROL_IPC_H
#define SLIVERBAR_CONTROL_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define CONTROL_ACTION_MAX 64

bool controlActionBuild(const char *module,
                        const char *operation,
                        char *action,
                        size_t actionSize);
bool controlActionValid(const char *action);
int controlSocketPath(char *path, size_t pathSize);
int controlServerOpen(const char *path);
void controlServerClose(int fd, const char *path);
int controlClientSend(const char *path, const char *action);
ssize_t controlServerReceive(int fd, char *action, size_t actionSize);

#endif
