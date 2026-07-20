#include "app_launcher.h"
#include "panel.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr,                                                          \
              "check failed at %s:%d: %s\n",                                   \
              __FILE__,                                                        \
              __LINE__,                                                        \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int writeFile(const char *path, const char *content, mode_t mode) {
  FILE *file = fopen(path, "w");
  if (!file)
    return -1;
  int result = fputs(content, file) < 0 || fclose(file) != 0 ? -1 : 0;
  if (!result && chmod(path, mode))
    result = -1;
  return result;
}

static int joinPath(char *output,
                    size_t outputSize,
                    const char *base,
                    const char *suffix) {
  size_t baseLength = strlen(base), suffixLength = strlen(suffix);
  if (baseLength + suffixLength + 1 > outputSize)
    return -1;
  memcpy(output, base, baseLength);
  memcpy(output + baseLength, suffix, suffixLength + 1);
  return 0;
}

static bool waitForFile(const char *path) {
  for (int i = 0; i < 100; i++) {
    if (access(path, F_OK) == 0)
      return true;
    usleep(10000);
  }
  return false;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "--gio-signal-probe") == 0) {
    sigset_t current;
    CHECK(sigprocmask(SIG_SETMASK, NULL, &current) == 0);
    return writeFile(
        argv[2], sigismember(&current, SIGCHLD) ? "blocked" : "ok", 0600);
  }
  if (!appLauncherHasGio())
    return 77;

  char root[] = "/tmp/sliverbar-app-defaults-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  char data[256], applications[256], config[256], calendarHandler[256];
  char imageHandler[256], calendarMarker[256], imageMarker[256];
  char calendarDesktop[256], imageDesktop[256], signalDesktop[256];
  char signalMarker[256], mimeapps[256], image[256];
  CHECK(joinPath(data, sizeof(data), root, "/data") == 0);
  CHECK(joinPath(applications, sizeof(applications), data, "/applications") ==
        0);
  CHECK(joinPath(config, sizeof(config), root, "/config") == 0);
  CHECK(joinPath(calendarHandler,
                 sizeof(calendarHandler),
                 root,
                 "/calendar-handler") == 0);
  CHECK(joinPath(imageHandler, sizeof(imageHandler), root, "/image-handler") ==
        0);
  CHECK(joinPath(
            calendarMarker, sizeof(calendarMarker), root, "/calendar.marker") ==
        0);
  CHECK(joinPath(imageMarker, sizeof(imageMarker), root, "/image.marker") == 0);
  CHECK(joinPath(calendarDesktop,
                 sizeof(calendarDesktop),
                 applications,
                 "/sliverbar-calendar.desktop") == 0);
  CHECK(joinPath(imageDesktop,
                 sizeof(imageDesktop),
                 applications,
                 "/sliverbar-image.desktop") == 0);
  CHECK(joinPath(signalDesktop,
                 sizeof(signalDesktop),
                 applications,
                 "/sliverbar-signal.desktop") == 0);
  CHECK(joinPath(signalMarker, sizeof(signalMarker), root, "/signal.marker") ==
        0);
  CHECK(joinPath(mimeapps, sizeof(mimeapps), config, "/mimeapps.list") == 0);
  CHECK(joinPath(image, sizeof(image), root, "/forecast.png") == 0);
  CHECK(mkdir(data, 0700) == 0);
  CHECK(mkdir(applications, 0700) == 0);
  CHECK(mkdir(config, 0700) == 0);

  char content[1024];
  snprintf(content,
           sizeof(content),
           "#!/bin/sh\nprintf calendar >'%s'\n",
           calendarMarker);
  CHECK(writeFile(calendarHandler, content, 0700) == 0);
  snprintf(content,
           sizeof(content),
           "#!/bin/sh\nprintf '%%s' \"$1\" >'%s'\n",
           imageMarker);
  CHECK(writeFile(imageHandler, content, 0700) == 0);
  snprintf(content,
           sizeof(content),
           "[Desktop Entry]\nType=Application\nName=Test Calendar\n"
           "Exec=%s\nMimeType=text/calendar;\n",
           calendarHandler);
  CHECK(writeFile(calendarDesktop, content, 0600) == 0);
  snprintf(content,
           sizeof(content),
           "[Desktop Entry]\nType=Application\nName=Test Image Viewer\n"
           "Exec=%s %%f\nMimeType=image/png;\n",
           imageHandler);
  CHECK(writeFile(imageDesktop, content, 0600) == 0);
  snprintf(content,
           sizeof(content),
           "[Desktop Entry]\nType=Application\nName=Signal Probe\n"
           "Exec=%s --gio-signal-probe %s\nTerminal=false\n",
           argv[0],
           signalMarker);
  CHECK(writeFile(signalDesktop, content, 0600) == 0);
  CHECK(writeFile(mimeapps,
                  "[Default Applications]\n"
                  "text/calendar=sliverbar-calendar.desktop;\n"
                  "image/png=sliverbar-image.desktop;\n",
                  0600) == 0);
  FILE *imageFile = fopen(image, "wb");
  static const unsigned char PNG_IMAGE[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
      0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  CHECK(imageFile != NULL);
  CHECK(fwrite(PNG_IMAGE, 1, sizeof(PNG_IMAGE), imageFile) ==
        sizeof(PNG_IMAGE));
  CHECK(fclose(imageFile) == 0);

  CHECK(setenv("XDG_DATA_HOME", data, 1) == 0);
  CHECK(setenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share", 1) == 0);
  CHECK(setenv("XDG_CONFIG_HOME", config, 1) == 0);

  PanelConfig panel;
  configDefaults(&panel);
  CHECK(appRoleAvailable(&panel, APP_ROLE_CALENDAR));
  CHECK(appLaunchRole(&panel, APP_ROLE_CALENDAR) == 0);
  CHECK(waitForFile(calendarMarker));
  CHECK(appCanOpenFile(image));
  CHECK(appOpenFile(image) == 0);
  CHECK(waitForFile(imageMarker));
  char openedPath[256];
  CHECK(readTextFile(imageMarker, openedPath, sizeof(openedPath)) == 0);
  CHECK(strcmp(openedPath, image) == 0);

  sigset_t blocked, previousMask;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGCHLD);
  CHECK(sigprocmask(SIG_BLOCK, &blocked, &previousMask) == 0);
  CHECK(appLaunchSpec(&panel, "desktop:sliverbar-signal.desktop") == 0);
  CHECK(sigprocmask(SIG_SETMASK, &previousMask, NULL) == 0);
  CHECK(waitForFile(signalMarker));
  char signalResult[16];
  CHECK(readTextFile(signalMarker, signalResult, sizeof(signalResult)) == 0);
  CHECK(strcmp(signalResult, "ok") == 0);

  snprintf(panel.calendar, sizeof(panel.calendar), "command:/bin/true");
  CHECK(appRoleAvailable(&panel, APP_ROLE_CALENDAR));
  CHECK(appLaunchRole(&panel, APP_ROLE_CALENDAR) == 0);
  snprintf(panel.calendar,
           sizeof(panel.calendar),
           "command:/definitely/missing/sliverbar");
  CHECK(!appRoleAvailable(&panel, APP_ROLE_CALENDAR));
  CHECK(appLaunchRole(&panel, APP_ROLE_CALENDAR) != 0);

  CHECK(unlink(calendarMarker) == 0);
  CHECK(unlink(imageMarker) == 0);
  CHECK(unlink(image) == 0);
  CHECK(unlink(mimeapps) == 0);
  CHECK(unlink(calendarDesktop) == 0);
  CHECK(unlink(imageDesktop) == 0);
  CHECK(unlink(signalDesktop) == 0);
  CHECK(unlink(signalMarker) == 0);
  CHECK(unlink(calendarHandler) == 0);
  CHECK(unlink(imageHandler) == 0);
  CHECK(rmdir(applications) == 0);
  CHECK(rmdir(data) == 0);
  CHECK(rmdir(config) == 0);
  CHECK(rmdir(root) == 0);
  return 0;
}
