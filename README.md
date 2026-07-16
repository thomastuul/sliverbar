# lemonbar-c

Portable C17 implementation of the panel logic in `../lemonbar`. It creates,
draws, and controls its X11 dock window directly; the Lemonbar program is not a
build-time or runtime dependency. The existing Bash implementation remains an
independent reference and fallback.

## Supported platform

Linux with X11 and bspwm. The native window uses XCB, Cairo, Pango, Fontconfig,
and the standard EWMH dock properties. Runtime backends are detected
dynamically and executed without a shell: `bspc`, `amixer` or `pactl`,
`xrandr`, optional `nmcli`, and optional desktop applications configured by the
user.

The target system needs the runtime libraries for XCB, Cairo, Pango, GLib, and
Fontconfig. Development headers and analysis tools are needed only in the
container. A build without the native development libraries still provides the
configuration-check and version CLI, but it cannot start a panel.

## Build

The reproducible development build runs in Docker (or rootless Podman) and
keeps compilers, development headers, XCB, and analysis tools out of the host:

```sh
./lemonbar_c/scripts/container-build.sh
```

Set `CONTAINER_ENGINE=podman` to use Podman. The script builds and tests three
presets: a native release, an explicit CLI-only dependency fallback, and an
ASan/UBSan native build. Native builds are rendered under Xvfb and their nested
left-, right-, and scroll-action routing is tested automatically.
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

The native release is dynamically linked. Inspect it with `ldd` before
distributing it to another system. `-DLEMONBAR_C_WITH_XCB=OFF` intentionally
builds the CLI-only variant used to verify that configuration and version
operations remain available without native development headers.

## Development checks

The repository contains project-local `clang-format` and `clang-tidy`
configuration files. Both tools run inside the container as part of:

```sh
./lemonbar_c/scripts/container-build.sh
```

The same command also runs the release, CLI-only, and sanitizer CTest suites.
No compiler, formatter, analyzer, or development headers are required on the
host.

## Run

```sh
lemonbar_c/build/container-release/lemonbar-panel \
  --config lemonbar_c/config/panel.conf
```

Print the centrally managed project version with:

```sh
lemonbar_c/build/container-release/lemonbar-panel --version
```

The program owns its X11 dock window and the `_NET_SYSTEM_TRAY_S0` selection,
subscribes to bspwm and X11 events, embeds tray clients through XEmbed, handles
clicks through a private action protocol, and shuts down all direct children.
It uses `$XDG_RUNTIME_DIR/lemonbar-c` for its lock. It does not evaluate shell
code.

The Bash panel and C panel must not be displayed simultaneously during visual
testing. The external `trayer` process must also be stopped before starting the
C panel because X11 permits only one system-tray manager per screen. `autostart`
is intentionally not changed by this project.

`super + b` continues to work because the native window publishes the
`lemonbar-c` application name. Tray icons are direct children of that window,
so they follow its visibility and occupy space calculated by the native
renderer. The C panel does not require the external `trayer` package.

## Architecture

- one `poll(2)` loop handles `timerfd`, `signalfd`, native mouse actions, bspwm,
  NetworkManager and X11;
- an XCB window with EWMH dock and strut properties replaces Lemonbar;
- Cairo and Pango render the existing block model with measured left, centered,
  and right-aligned regions;
- mouse actions use a private `|`-separated protocol and are never evaluated by
  a shell;
- CPU, battery, clock, screencast, network state and cache parsing are native C;
- XCB property events update the active-window title without polling;
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
| `xtmon.sh`, `title_server.sh` | native XCB property events |
| clock, CPU, battery, screencast | native `/proc`, `/sys`, time and XDG logic |
| volume and brightness | backend detection plus validated action protocol |
| network worker | `/sys/class/net`, optional nmcli query and monitor |
| weather worker | non-blocking child, atomic JSON/PNG caches |
| trayer block | native XEmbed tray manager and direct child-window layout |
| launcher, power and terminal clicks | detached, argument-based exec |

The Bash directory is not read or executed by `lemonbar-panel`.
