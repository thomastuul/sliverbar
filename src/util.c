#include "panel.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int mkdirP(const char *path, mode_t mode) {
  char tmp[PANEL_PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char *p = tmp + 1; *p; p++)
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, mode) && errno != EEXIST)
        return -1;
      *p = '/';
    }
  return mkdir(tmp, mode) && errno != EEXIST ? -1 : 0;
}

static int verifyPrivateDirectory(const char *path) {
  struct stat status;
  if (lstat(path, &status))
    return -1;
  if (!S_ISDIR(status.st_mode)) {
    errno = ENOTDIR;
    return -1;
  }
  if (status.st_uid != getuid() || (status.st_mode & 077U)) {
    errno = EACCES;
    return -1;
  }
  return 0;
}

static int ensurePrivateDirectory(const char *path) {
  if (mkdir(path, 0700) && errno != EEXIST)
    return -1;
  return verifyPrivateDirectory(path);
}

int sliverbarRuntimeBaseDirectory(char *path, size_t size, bool create) {
  if (!path || !size) {
    errno = EINVAL;
    return -1;
  }
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  char fallback[64];
  if (!runtime || !*runtime) {
    int length = snprintf(
        fallback, sizeof(fallback), "/tmp/sliverbar-%ld", (long)getuid());
    if (length < 0 || (size_t)length >= sizeof(fallback)) {
      errno = ENAMETOOLONG;
      return -1;
    }
    runtime = fallback;
  }
  int length = snprintf(path, size, "%s", runtime);
  if (length < 0 || (size_t)length >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  if (create) {
    return ensurePrivateDirectory(path);
  }
  return verifyPrivateDirectory(path);
}

int sliverbarRuntimeDirectory(char *path, size_t size, bool create) {
  char runtime[PANEL_PATH_MAX];
  if (sliverbarRuntimeBaseDirectory(runtime, sizeof(runtime), create))
    return -1;
  int length = snprintf(path, size, "%s/sliverbar", runtime);
  if (length < 0 || (size_t)length >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  if (create) {
    return ensurePrivateDirectory(path);
  } else if (verifyPrivateDirectory(runtime)) {
    return -1;
  }
  return verifyPrivateDirectory(path);
}

int readTextFile(const char *path, char *buf, size_t size) {
  if (!size)
    return -1;
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t n = read(fd, buf, size - 1);
  int saved = errno;
  close(fd);
  if (n < 0) {
    errno = saved;
    return -1;
  }
  buf[n] = '\0';
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = '\0';
  return 0;
}

int writeAtomic(const char *path, const char *data, mode_t mode) {
  char tmp[PANEL_PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
  if (fd < 0)
    return -1;
  size_t len = strlen(data), off = 0;
  while (off < len) {
    ssize_t n = write(fd, data + off, len - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      close(fd);
      unlink(tmp);
      return -1;
    }
    off += (size_t)n;
  }
  if (fsync(fd) || close(fd) || rename(tmp, path)) {
    int e = errno;
    unlink(tmp);
    errno = e;
    return -1;
  }
  return 0;
}

bool commandExists(const char *name) {
  if (strchr(name, '/'))
    return access(name, X_OK) == 0;
  const char *path = getenv("PATH");
  if (!path)
    return false;
  char copybuf[PANEL_PATH_MAX];
  snprintf(copybuf, sizeof(copybuf), "%s", path);
  char *save = NULL;
  for (char *d = strtok_r(copybuf, ":", &save); d;
       d = strtok_r(NULL, ":", &save)) {
    char p[PANEL_PATH_MAX];
    snprintf(p, sizeof(p), "%s/%s", *d ? d : ".", name);
    if (access(p, X_OK) == 0)
      return true;
  }
  return false;
}

int runCapture(char *const argv[], char *out, size_t size, int timeoutMs) {
  if (size)
    out[0] = '\0';
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK))
    return -1;
  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    int null = open("/dev/null", O_WRONLY);
    if (null >= 0)
      dup2(null, STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    if (setenv("LC_ALL", "C", 1))
      _exit(127);
    execvp(argv[0], argv);
    _exit(127);
  }
  close(pipefd[1]);
  size_t used = 0;
  int elapsed = 0, status = 0;
  while (elapsed < timeoutMs) {
    struct pollfd p = {.fd = pipefd[0], .events = POLLIN};
    int step = timeoutMs - elapsed;
    if (step > 50)
      step = 50;
    int pr = poll(&p, 1, step);
    elapsed += step;
    if (pr > 0 && (p.revents & POLLIN)) {
      ssize_t n = read(pipefd[0], out + used, size ? size - used - 1 : 0);
      if (n > 0)
        used += (size_t)n;
    }
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid)
      break;
    if (w < 0 && errno != EINTR)
      break;
  }
  if (waitpid(pid, &status, WNOHANG) == 0) {
    kill(pid, SIGTERM);
    for (int i = 0; i < 10 && waitpid(pid, &status, WNOHANG) == 0; i++)
      usleep(10000);
    if (waitpid(pid, &status, WNOHANG) == 0) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
    }
  }
  for (;;) {
    ssize_t n = read(
        pipefd[0], out + used, size && used < size - 1 ? size - used - 1 : 0);
    if (n > 0)
      used += (size_t)n;
    else
      break;
  }
  close(pipefd[0]);
  if (size)
    out[used] = '\0';
  while (used && (out[used - 1] == '\n' || out[used - 1] == '\r'))
    out[--used] = '\0';
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int spawnDetached(char *const argv[]) {
  pid_t p = fork();
  if (p < 0)
    return -1;
  if (!p) {
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    struct sigaction defaults = {.sa_handler = SIG_DFL};
    sigemptyset(&defaults.sa_mask);
    sigaction(SIGCHLD, &defaults, NULL);
    sigaction(SIGPIPE, &defaults, NULL);
    sigaction(SIGINT, &defaults, NULL);
    sigaction(SIGTERM, &defaults, NULL);
    sigaction(SIGHUP, &defaults, NULL);
    setsid();
    pid_t q = fork();
    if (q < 0)
      _exit(127);
    if (q)
      _exit(0);
    int n = open("/dev/null", O_RDWR);
    if (n >= 0) {
      dup2(n, 0);
      dup2(n, 1);
      dup2(n, 2);
      if (n > 2)
        close(n);
    }
    execvp(argv[0], argv);
    _exit(127);
  }
  int st;
  return waitpid(p, &st, 0) < 0 ? -1 : 0;
}

pid_t spawnTracked(char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0)
    return -1;
  if (!pid) {
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    struct sigaction defaults = {.sa_handler = SIG_DFL};
    sigemptyset(&defaults.sa_mask);
    sigaction(SIGCHLD, &defaults, NULL);
    sigaction(SIGPIPE, &defaults, NULL);
    sigaction(SIGINT, &defaults, NULL);
    sigaction(SIGTERM, &defaults, NULL);
    sigaction(SIGHUP, &defaults, NULL);
    setsid();
    int nullFd = open("/dev/null", O_RDWR);
    if (nullFd >= 0) {
      dup2(nullFd, STDIN_FILENO);
      dup2(nullFd, STDOUT_FILENO);
      dup2(nullFd, STDERR_FILENO);
      if (nullFd > STDERR_FILENO)
        close(nullFd);
    }
    execvp(argv[0], argv);
    _exit(127);
  }
  return pid;
}

void shellQuoteAction(const char *in, char *out, size_t size) {
  size_t n = 0;
  for (; *in && n + 2 < size; in++) {
    char c = *in;
    if (c == ':' || c == '|' || c == '%' || c == '\n' || c == '\r')
      c = '_';
    out[n++] = c;
  }
  out[n] = '\0';
}

void logMessage(const char *level, const char *fmt, ...) {
  char ts[32];
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(ts, sizeof(ts), "%F %T", &tm);
  fprintf(stderr, "%s\tsliverbar\t%s: ", ts, level);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
