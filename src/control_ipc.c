#include "control_ipc.h"

#include "panel.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static bool operationAllowed(const char *module, const char *operation) {
  if (!strcmp(module, "volume"))
    return !strcmp(operation, "up") || !strcmp(operation, "down") ||
           !strcmp(operation, "toggle");
  if (!strcmp(module, "brightness"))
    return !strcmp(operation, "up") || !strcmp(operation, "down");
  if (!strcmp(module, "refresh"))
    return !strcmp(operation, "volume") || !strcmp(operation, "brightness");
  return false;
}

bool controlActionBuild(const char *module,
                        const char *operation,
                        char *action,
                        size_t actionSize) {
  if (!module || !operation || !action || !actionSize ||
      !operationAllowed(module, operation))
    return false;
  int length = snprintf(action, actionSize, "%s|%s", module, operation);
  return length > 0 && (size_t)length < actionSize;
}

bool controlActionValid(const char *action) {
  if (!action || !*action || strlen(action) >= CONTROL_ACTION_MAX)
    return false;
  char copy[CONTROL_ACTION_MAX];
  snprintf(copy, sizeof(copy), "%s", action);
  char *separator = strchr(copy, '|');
  if (!separator || strchr(separator + 1, '|'))
    return false;
  *separator = '\0';
  char expected[CONTROL_ACTION_MAX];
  return controlActionBuild(copy, separator + 1, expected, sizeof(expected)) &&
         !strcmp(action, expected);
}

int controlSocketPath(char *path, size_t pathSize) {
  if (!path || !pathSize) {
    errno = EINVAL;
    return -1;
  }
  char directory[PANEL_PATH_MAX];
  if (sliverbarRuntimeDirectory(directory, sizeof(directory), false))
    return -1;
  int length = snprintf(path, pathSize, "%s/control.sock", directory);
  if (length < 0 || (size_t)length >= pathSize) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

static int socketAddress(const char *path, struct sockaddr_un *address) {
  if (!path || !address) {
    errno = EINVAL;
    return -1;
  }
  size_t length = strlen(path);
  if (length >= sizeof(address->sun_path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memset(address, 0, sizeof(*address));
  address->sun_family = AF_UNIX;
  memcpy(address->sun_path, path, length + 1);
  return 0;
}

int controlServerOpen(const char *path) {
  struct sockaddr_un address;
  if (socketAddress(path, &address))
    return -1;

  struct stat status;
  if (!lstat(path, &status)) {
    if (!S_ISSOCK(status.st_mode)) {
      errno = EEXIST;
      return -1;
    }
    if (unlink(path))
      return -1;
  } else if (errno != ENOENT) {
    return -1;
  }

  int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return -1;
  if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) ||
      chmod(path, 0600)) {
    int saved = errno;
    close(fd);
    unlink(path);
    errno = saved;
    return -1;
  }
  return fd;
}

void controlServerClose(int fd, const char *path) {
  if (fd >= 0)
    close(fd);
  if (path)
    unlink(path);
}

int controlClientSend(const char *path, const char *action) {
  struct sockaddr_un address;
  if (!action || socketAddress(path, &address)) {
    if (action)
      return -1;
    errno = EINVAL;
    return -1;
  }
  size_t length = strlen(action);
  if (!length || length >= CONTROL_ACTION_MAX) {
    errno = EINVAL;
    return -1;
  }
  int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  ssize_t written = sendto(fd,
                           action,
                           length,
                           0,
                           (const struct sockaddr *)&address,
                           sizeof(address));
  int saved = errno;
  close(fd);
  if (written != (ssize_t)length) {
    errno = written < 0 ? saved : EIO;
    return -1;
  }
  return 0;
}

ssize_t controlServerReceive(int fd, char *action, size_t actionSize) {
  if (fd < 0 || !action || actionSize < 2) {
    errno = EINVAL;
    return -1;
  }
  ssize_t received = recv(fd, action, actionSize - 1, 0);
  if (received < 0)
    return -1;
  action[received] = '\0';
  return received;
}
