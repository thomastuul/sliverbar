#include "panel.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--signal-probe") == 0) {
        sigset_t mask;
        struct sigaction action;
        sigprocmask(SIG_SETMASK, NULL, &mask);
        sigaction(SIGPIPE, NULL, &action);
        FILE *probe = fopen(argv[2], "w");
        if (!probe)
            return 2;
        fputs(!sigismember(&mask, SIGCHLD) && action.sa_handler == SIG_DFL ? "ok" : "bad", probe);
        return fclose(probe) == 0 ? 0 : 2;
    }

    panel_config cfg;
    config_defaults(&cfg);
    CHECK(cfg.height == 25);
    CHECK(cfg.volume_step == 2);
    CHECK(strcmp(cfg.color_panel_bg, "#191A21") == 0);
    CHECK(strcmp(cfg.color_bg, "#282A36") == 0);
    CHECK(strstr(cfg.font, "size=13") != NULL);
    CHECK(strstr(cfg.icon_font, "size=13") != NULL);

    char path[] = "/tmp/lemonbar-c-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    const char text[] = "height=31\nvolume_step=4\ncolor_bg=#000000\n";
    CHECK(write(fd, text, sizeof(text) - 1) == (ssize_t)(sizeof(text) - 1));
    CHECK(close(fd) == 0);

    char error[256];
    CHECK(config_load(&cfg, path, error, sizeof(error)) == 0);
    CHECK(cfg.height == 31);
    CHECK(cfg.volume_step == 4);
    CHECK(strcmp(cfg.color_bg, "#000000") == 0);
    CHECK(strcmp(cfg.color_occupied, "#ff5555") == 0);
    CHECK(unlink(path) == 0);

    char quoted[32];
    shell_quote_action("a:b|c%\n", quoted, sizeof(quoted));
    CHECK(strcmp(quoted, "a_b_c__") == 0);

    char signal_path[] = "/tmp/lemonbar-c-signals-XXXXXX";
    fd = mkstemp(signal_path);
    CHECK(fd >= 0);
    CHECK(close(fd) == 0);
    CHECK(unlink(signal_path) == 0);
    sigset_t blocked, old_mask;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGCHLD);
    CHECK(sigprocmask(SIG_BLOCK, &blocked, &old_mask) == 0);
    struct sigaction ignored = {.sa_handler = SIG_IGN}, old_pipe;
    sigemptyset(&ignored.sa_mask);
    CHECK(sigaction(SIGPIPE, &ignored, &old_pipe) == 0);
    char *probe_argv[] = {argv[0], "--signal-probe", signal_path, NULL};
    CHECK(spawn_detached(probe_argv) == 0);
    CHECK(sigprocmask(SIG_SETMASK, &old_mask, NULL) == 0);
    CHECK(sigaction(SIGPIPE, &old_pipe, NULL) == 0);
    for (int i = 0; i < 100 && access(signal_path, F_OK) != 0; i++)
        usleep(10000);
    char signal_result[16];
    CHECK(read_text_file(signal_path, signal_result, sizeof(signal_result)) == 0);
    CHECK(strcmp(signal_result, "ok") == 0);
    CHECK(unlink(signal_path) == 0);

    panel_state state = {0};
    strcpy(state.launcher, "L");
    strcpy(state.workspace, "W");
    strcpy(state.title, "T");
    strcpy(state.clock, "C");
    char rendered[128];
    render_panel(&state, rendered, sizeof(rendered));
    CHECK(strcmp(rendered, "%{l}LW%{c}T%{r}C\n") == 0);

    module_workspace(&cfg, &state, "WMDP-3:O1:o2:f3:LT:TT:G");
    CHECK(strstr(state.workspace, "%{F#69FF94}%{B#191A21}") != NULL);
    CHECK(strstr(state.workspace, "%{F#ff5555}%{B#191A21}") != NULL);
    CHECK(strstr(state.workspace, "[TILED]") != NULL);

    char invalid_path[] = "/tmp/lemonbar-c-invalid-XXXXXX";
    fd = mkstemp(invalid_path);
    CHECK(fd >= 0);
    const char invalid[] = "unknown_key=value\n";
    CHECK(write(fd, invalid, sizeof(invalid) - 1) == (ssize_t)(sizeof(invalid) - 1));
    CHECK(close(fd) == 0);
    CHECK(config_load(&cfg, invalid_path, error, sizeof(error)) != 0);
    CHECK(unlink(invalid_path) == 0);
    puts("ok");
    return 0;
}
