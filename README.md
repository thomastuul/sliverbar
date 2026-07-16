# lemonbar-c

Portable C17 implementation of the panel logic in `../lemonbar`. The existing
Bash implementation remains an independent reference and fallback.

## Supported platform

Linux with X11, bspwm and lemonbar. XCB is used when its development files are
available; otherwise the build provides an `xprop` fallback. Runtime backends
are detected dynamically and executed without a
shell: `bspc`, `lemonbar`, `amixer` or `pactl`, `xrandr`, optional `nmcli`, and
optional desktop applications configured by the user.

## Build

The reproducible development build runs in Docker (or rootless Podman) and
keeps compilers, development headers, XCB, and analysis tools out of the host:

```sh
./lemonbar_c/scripts/container-build.sh
```

Set `CONTAINER_ENGINE=podman` to use Podman. The script builds and tests three
presets: an XCB release, an explicit non-XCB fallback, and an ASan/UBSan build.
The host-owned release binary is written to:

```text
lemonbar_c/build/container-release/lemonbar-panel
```

For a local build without the container:

```sh
cmake -S lemonbar_c -B build/lemonbar_c -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/lemonbar_c --parallel
ctest --test-dir build/lemonbar_c --output-on-failure
```

Sanitizers can be enabled with `-DLEMONBAR_C_SANITIZERS=ON`. Install with
`cmake --install build/lemonbar_c`.

The XCB release links dynamically to libc and libxcb on the target host. The
fallback can be built with `-DLEMONBAR_C_WITH_XCB=OFF` and does not link to
libxcb. Inspect a release with `ldd` before distributing it to another system.

## Development checks

The repository contains project-local `clang-format` and `clang-tidy`
configuration files. Run the checks from the repository root:

```sh
clang-format --dry-run --Werror lemonbar_c/{include,src,tests}/*.[ch]
clang-tidy -quiet -p build/lemonbar_c lemonbar_c/src/*.c lemonbar_c/tests/*.c
```

## Run

```sh
build/lemonbar_c/lemonbar-panel \
  --config build/lemonbar_c/panel.conf
```

Print the centrally managed project version with:

```sh
build/lemonbar_c/lemonbar-panel --version
```

The program owns Lemonbar, subscribes to bspwm and X11 events, handles clicks
through a private action protocol, and shuts down all direct children. It uses
`$XDG_RUNTIME_DIR/lemonbar-c` for its lock. It does not evaluate shell code.

The Bash panel and C panel must not be displayed simultaneously during visual
testing. `autostart` is intentionally not changed by this project.

## Architecture

- one `poll(2)` loop handles `timerfd`, `signalfd`, Lemonbar actions, bspwm,
  NetworkManager and X11;
- Lemonbar actions use a private `|`-separated protocol and are never evaluated
  by a shell;
- CPU, battery, clock, screencast, network state and cache parsing are native C;
- XCB is preferred for active-window events, with persistent `xprop -spy`
  watchers as a portable, event-driven fallback;
- weather downloads run in a supervised child and are atomically published;
- optional programs are detected at runtime and executed with explicit argv.

## Configuration

The installed example is an intentionally simple `key=value` file. Unknown or
invalid keys fail validation. Paths are derived from `HOME`, `XDG_CACHE_HOME`
and `XDG_RUNTIME_DIR` unless explicitly configured. Run
`lemonbar-panel --check-config --config PATH` before starting a panel.

## Feature mapping

| Bash component | C implementation |
| --- | --- |
| `start.sh`, `sighandler.sh` | supervisor, `poll`, `timerfd`, `signalfd` |
| `events.sh`, workspace block | persistent `bspc subscribe report` parser |
| `xtmon.sh`, `title_server.sh` | XCB property events or persistent `xprop -spy` watchers |
| clock, CPU, battery, screencast | native `/proc`, `/sys`, time and XDG logic |
| volume and brightness | backend detection plus validated action protocol |
| network worker | `/sys/class/net`, optional nmcli query and monitor |
| weather worker | non-blocking child, atomic JSON/PNG caches |
| trayer block | X11 hints query and periodic offset refresh |
| launcher, power and terminal clicks | detached, argument-based exec |

The Bash directory is not read or executed by `lemonbar-panel`.
