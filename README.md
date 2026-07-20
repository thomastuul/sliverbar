# Sliverbar

Sliverbar is a lightweight C17 panel for Linux desktops running X11 and an
EWMH-compatible window manager. bspwm has an optional enhanced backend.
It creates, draws, and controls its dock window directly. The existing Bash
panel remains an independent reference and fallback.

## Supported platform

Linux with X11 and an EWMH-compatible window manager. The native window uses
XCB, Cairo, Pango, Fontconfig, and standard EWMH properties. Runtime backends
are detected dynamically and executed without a shell: optional `bspc`,
`amixer` or `pactl`,
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
./scripts/container-build.sh
```

Set `CONTAINER_ENGINE=podman` to use Podman. The script builds and tests three
presets: a native release, an explicit CLI-only dependency fallback, and an
ASan/UBSan native build. Native builds are rendered under Xvfb and their nested
left-, right-, and scroll-action routing is tested automatically.
The host-owned release binary is written to:

```text
build/container-release/sliverbar
```

For a local build without the container:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/local --parallel
ctest --test-dir build/local --output-on-failure
```

Sanitizers can be enabled with `-DSLIVERBAR_SANITIZERS=ON`. Install with
`cmake --install build/local`.

The native release is dynamically linked. Inspect it with `ldd` before
distributing it to another system. `-DSLIVERBAR_WITH_XCB=OFF` intentionally
builds the CLI-only variant used to verify that configuration and version
operations remain available without native development headers.

## Development checks

The repository contains project-local `clang-format` and `clang-tidy`
configuration files. Both tools run inside the container as part of:

```sh
./scripts/container-build.sh
```

The same command also runs the release, CLI-only, and sanitizer CTest suites.
No compiler, formatter, analyzer, or development headers are required on the
host.

## Run

```sh
build/container-release/sliverbar --config config/panel.conf
```

Print the centrally managed project version with:

```sh
build/container-release/sliverbar --version
```

The program owns its X11 dock window and the `_NET_SYSTEM_TRAY_S0` selection,
subscribes to workspace and X11 events, embeds tray clients through XEmbed, handles
clicks through a private action protocol, and shuts down all direct children.
It uses `$XDG_RUNTIME_DIR/sliverbar` for its lock. It does not evaluate shell
code.

The Bash panel and C panel must not be displayed simultaneously during visual
testing. The external `trayer` process must also be stopped before starting the
C panel because X11 permits only one system-tray manager per screen. `autostart`
is intentionally not changed by this project.

Visibility keybindings should target the `sliverbar` application name. A
dedicated top-level tray host gives XEmbed clients correct screen coordinates
for their popups and follows the panel's visibility internally. The renderer
reserves the host's measured width. Sliverbar does not require the external
`trayer` package.

## Architecture

- one `poll(2)` loop handles `timerfd`, `signalfd`, native mouse actions,
  optional bspwm reports, NetworkManager and X11;
- an XCB window with EWMH dock and strut properties provides the panel surface;
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
`sliverbar --check-config --config PATH` before starting a panel.

Without `--config` or `SLIVERBAR_CONFIG`, configuration is searched in
`$XDG_CONFIG_HOME/sliverbar/panel.conf`, then
`$HOME/.config/sliverbar/panel.conf`, the installed system configuration, and
finally the local development configuration. No bspwm-specific path is
implicitly selected.

Each main block has a `module_NAME=auto|enabled|disabled` switch. `auto` hides
blocks whose data source or optional runtime command is unavailable. The
supported names are `clock`, `title`, `cpu`, `battery`, `screencast`, `volume`,
`network`, `brightness`, `weather`, `launcher`, `tray`, and `power`. For
example, a deliberately minimal panel can start from the example configuration
and set every module except `clock` and `title` to `disabled`, plus
`workspace_backend=none`.

The default text font is the generic Pango `Monospace` family. An empty
`icon_font` uses the regular font and Pango's installed fallback fonts; a Nerd
Font remains an optional visual enhancement. Weather is disabled automatically
until `location` is configured. The example assumes neither a terminal nor a
launcher or power-menu script.

### Application roles

Clicks are dispatched without shell evaluation. `system_monitor`,
`network_settings`, `volume_settings`, and `calendar` default to `auto`.
Overrides accept `desktop:ID.desktop`, `command:PROGRAM ARGUMENTS`, or
`terminal:PROGRAM ARGUMENTS`; quoted argv syntax is parsed but never evaluated
as shell code. Desktop entries and default file handlers use GIO when it was
available at build time, preserving `Exec` field codes, `TryExec`, terminal
metadata, and D-Bus activation.

Automatic resolution first honors the explicit override. Calendar then uses
the registered `text/calendar` handler. Other roles have no standardized XDG
default and therefore use a documented shortlist: common graphical system
monitors before `btop`/`htop`/`top`, NetworkManager's editor before `nmtui`,
and `pavucontrol` before terminal mixers. Terminal programs prefer
`xdg-terminal-exec`, then a simple `$TERMINAL`, followed by known terminals
with their appropriate command separator. If no valid target exists, the block
remains visible but has no click action. Forecast images are opened through the
registered default file handler rather than a hard-coded image viewer.

`workspace_backend=auto` prefers bspwm's report stream when a reachable `bspc`
is available and otherwise uses EWMH. `ewmh` forces the portable backend,
`bspwm` requests bspwm with an automatic EWMH fallback, and `none` hides the
workspace block. The panel remains operational if no workspace properties are
published.

## Feature mapping

| Bash component | C implementation |
| --- | --- |
| `start.sh`, `sighandler.sh` | supervisor, `poll`, `timerfd`, `signalfd` |
| `events.sh`, workspace block | EWMH backend with optional persistent `bspc subscribe report` backend |
| `xtmon.sh`, `title_server.sh` | native XCB property events |
| clock, CPU, battery, screencast | native `/proc`, `/sys`, time and XDG logic |
| volume and brightness | backend detection plus validated action protocol |
| network worker | `/sys/class/net`, optional nmcli query and monitor |
| weather worker | non-blocking child, atomic JSON/PNG caches |
| trayer block | native XEmbed tray manager and direct child-window layout |
| launcher, power and terminal clicks | detached, argument-based exec |

The Bash directory is not read or executed by `sliverbar`.
