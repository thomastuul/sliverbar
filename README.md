# Sliverbar

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
![C17](https://img.shields.io/badge/C-C17-blue.svg)

Sliverbar is a lightweight C17 panel for Linux desktops running X11 and an
EWMH-compatible window manager. bspwm has an optional enhanced backend.
It creates, draws, and controls its dock window directly. The existing Bash
panel remains an independent reference and fallback.

For a block-by-block overview, mouse controls, configuration, and the
interaction between Sliverbar, bspwm, and sxhkd, see [USAGE.md](USAGE.md).

## Authors and license

Sliverbar was created and is maintained by Thomas Tuul, with substantial
development assistance from OpenAI Codex. See `AUTHORS.md` for the
authorship and acknowledgement notice.

Sliverbar is licensed under the GNU General Public License, version 3. See the
`LICENSE` file for the full license text.

## Supported platform

Linux with X11 and an EWMH-compatible window manager. The native window uses
XCB, Cairo, Pango, Fontconfig, and standard EWMH properties. Runtime backends
are detected dynamically and executed without a shell: optional `bspc`,
`amixer` or `pactl`,
`xrandr`, optional `nmcli`, and optional desktop applications configured by the
user.

The native binary is dynamically linked and requires glibc plus XCB, Cairo,
Pango/PangoCairo, GLib/GIO, Fontconfig, and the shared MIME database provided
by `shared-mime-info`. RandR and `xkbcommon-x11` are used when present at build
time for monitor handling and keyboard-driven popups.
Development headers and analysis tools are needed only in the container. A
build without native XCB development libraries still provides configuration,
diagnostic, and version commands, but it cannot start a panel.

Artifacts are architecture- and distribution-specific. The standard container
currently produces an x86-64 Debian 13/glibc binary, not a universal Linux
binary. Build in the oldest supported target environment when a particular
glibc baseline is required.

## Build

The reproducible development build runs in Docker (or rootless Podman) and
keeps compilers, development headers, XCB, and analysis tools out of the host:

```sh
./scripts/container-build.sh
```

Set `CONTAINER_ENGINE=podman` to use Podman. The script builds and tests native
GCC and Clang releases, an explicit CLI-only dependency fallback, and an
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
`cmake --install build/local`. The same CMake install rules place the binary,
system configuration, and `sliverbar(1)` manual page in both Debian and Fedora
packages.

After the required restricted, read-only security review, build and validate
both packages with:

```sh
SLIVERBAR_STATIC_REVIEWED=1 ./scripts/package-build.sh
```

Set `CONTAINER_ENGINE=podman` to package with rootless Podman. The resulting
`.deb` and `.rpm` are host-owned files below `build/package/`, which is ignored
by Git. Package production is deliberately separate from a routine build. A
full Codex Security scan remains optional and requires explicit user approval.

For GitHub publishing, use the repository workflows:

- `.github/workflows/test.yml` runs `./scripts/test.sh` on pushes and pull
  requests.
- `.github/workflows/release.yml` builds both package formats on tags matching
  `v*` and uploads the resulting `.deb`, `.rpm`, and `SHA256SUMS` files as
  release assets.

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

Manual Fedora and Arch compatibility containers are available through
`./scripts/compatibility-build.sh`. They run the same GCC, Clang, CLI-only,
sanitizer, and Xvfb checks, but are not yet release-gating platforms.

## Run

```sh
build/container-release/sliverbar --config config/panel.conf
```

Print the centrally managed project version with:

```sh
build/container-release/sliverbar --version
```

Validate configuration or inspect runtime detection without starting a panel:

```sh
sliverbar --config ~/.config/sliverbar/panel.conf --check-config
sliverbar --config ~/.config/sliverbar/panel.conf --diagnose
```

Send a validated action to the running panel:

```sh
sliverbar --action volume up
sliverbar --action brightness down
sliverbar --action refresh volume
```

The installed usage manual is available with:

```sh
man sliverbar
```

Diagnostics report the selected configuration and display, workspace backend,
optional programs, configured fonts, terminal and application roles, launcher
catalog, WLAN source and raw value, weather locations, logind power actions,
and inhibitor backend.

The program owns its X11 dock window and the `_NET_SYSTEM_TRAY_Sn` selection
for the selected X screen,
subscribes to workspace and X11 events, embeds tray clients through XEmbed, handles
clicks through a private action protocol, and shuts down all direct children.
It uses `$XDG_RUNTIME_DIR/sliverbar` for its lock and user-only control socket.
The control socket accepts only the documented volume, brightness, and refresh
actions. Sliverbar does not evaluate shell code.

The Bash panel and C panel must not be displayed simultaneously during visual
testing. The external `trayer` process must also be stopped before starting the
native panel. Existing desktop keybindings are intentionally not changed by
this project.

Visibility keybindings should target the `sliverbar` application name. A
dedicated top-level tray host gives XEmbed clients correct screen coordinates
for their popups and follows the panel's visibility internally. It uses the
configured block background and advertises itself as a dock window so
compositors render it as a seamless part of the panel. The renderer reserves
the host's measured width. Sliverbar does not require the external `trayer`
package.

## Architecture

The technical architecture, build targets, runtime flow, optional integrations,
process-safety model, testing architecture, and extension guidelines are
documented in [docs/architecture.md](docs/architecture.md).

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

For local Codex use, `sliverbar` also ships a helper wrapper at
`scripts/codex-context7.sh`. It loads `CONTEXT7_API_KEY` from
`$HOME/.config/bspwm/sliverbar/.codex/context7.env` by default, or from the
path given in `CONTEXT7_ENV_FILE`, and then starts Codex with your arguments.
That keeps the API token out of `config.toml` while still making Context7
available in this repository.

The portable MCP configuration is provided as `.codex/config.toml.example`.
Codex does not load the example file automatically, and it loads project-local
`.codex/config.toml` only after the repository has been marked as trusted.
Review and accept the project trust prompt before relying on this
configuration. If the checkout must remain untrusted, merge the same MCP blocks
into the user-level `~/.codex/config.toml` instead.

If no local project configuration exists, copy the example before starting
Codex:

```bash
cp .codex/config.toml.example .codex/config.toml
```

If `.codex/config.toml` already exists, merge the `mcp_servers.context7` and
`mcp_servers.openaiDeveloperDocs` blocks instead of overwriting the file. Both
the active configuration and `.codex/context7.env` are ignored so personal
settings and API keys remain local.

Each main block has a `module_NAME=auto|enabled|disabled` switch. `auto` hides
blocks whose data source or optional runtime command is unavailable. The
supported names are `clock`, `title`, `cpu`, `battery`, `screencast`, `volume`,
`network`, `brightness`, `weather`, `timer`, `inhibitor`, `launcher`, `tray`, and
`power`. For
example, a deliberately minimal panel can start from the example configuration
and set every module except `clock` and `title` to `disabled`, plus
`workspace_backend=none`.

The compiled text-font fallback is the generic Pango `Monospace` family; the
shipped example selects `JetBrainsMono Nerd Font Mono`. An empty `icon_font`
uses the regular font and Pango's installed fallback fonts, so a Nerd Font
remains optional. The shipped example already defines several
`weather_location` entries. A custom configuration without either
`weather_location` or the legacy `location` key hides the weather block. The
example assumes neither a terminal nor a launcher or power-menu script.

`monitor=primary` selects the primary RandR monitor. A monitor name or
zero-based RandR index selects one explicitly; `all` spans the complete X
screen. Sliverbar recomputes its window, strut, tray, and popup bounds when the
layout changes. If RandR or the configured monitor is unavailable, the root
screen is the safe fallback.

### Application roles

Clicks are dispatched without shell evaluation. `system_monitor`,
`network_settings`, `volume_settings`, `calendar`, and `tasks` default to
`auto`.
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
remains visible but has no click action.

### Read-only EDS agenda

Set `agenda_provider=eds` to add a native read-only agenda to the date/time
block. A right click opens it directly at the panel edge; the existing left
click still opens the calendar application. Events, tasks, overdue tasks, and
source names use separately configurable Dracula colors. The list defaults to
7 local calendar days, 10 rows, at most 2 undated tasks, a 300-second
consistency refresh, and a 720-pixel popup width. Event titles wrap instead of
being truncated, and a second detail line shows the organizer and calendar
source when available.

Sliverbar reads only sources already exposed by Evolution Data Server. It does
not implement DAV or OAuth, read Evolution/Thunderbird database files, store
credentials, or write PIM data. List safe source IDs and display names with
`sliverbar --list-pim-sources`, then select all enabled sources with `*`,
disable a type with `none`, or repeat explicit source IDs:

```ini
agenda_provider=eds
agenda_calendar_source=*
agenda_task_source=*
agenda_days=7
agenda_max_items=10
agenda_max_undated_tasks=2
agenda_refresh_interval=300
agenda_popup_width=720
agenda_show_source=true
agenda_event_color=#8BE9FD
agenda_task_color=#FFB86C
agenda_overdue_color=#FF5555
agenda_source_color=#6272A4
```

`agenda_provider=none` is the portable default. When EDS support is absent or
all selected sources fail, startup logs an unavailable state and right click
does nothing. Reachable sources remain usable during a partial failure. The
optional build switch is `-DSLIVERBAR_EDS=AUTO|ON|OFF`; `ON` requires
`libecal-2.0` and `libedataserver-1.2`.

`workspace_backend=auto` prefers bspwm's report stream when a reachable `bspc`
is available and otherwise uses EWMH. `ewmh` forces the portable backend,
`bspwm` requests bspwm with an automatic EWMH fallback, and `none` hides the
workspace block. The panel remains operational if no workspace properties are
published.

A minimal generic override can disable every data-dependent block while
keeping clock and title:

```ini
workspace_backend=none
module_clock=enabled
module_title=enabled
module_cpu=disabled
module_battery=disabled
module_screencast=disabled
module_volume=disabled
module_network=disabled
module_brightness=disabled
module_weather=disabled
module_timer=disabled
module_inhibitor=disabled
module_launcher=disabled
module_tray=disabled
module_power=disabled
```

For enhanced bspwm workspaces while retaining portable fallbacks:

```ini
workspace_backend=bspwm
monitor=primary
module_launcher=auto
module_tray=auto
```

### Native launcher and power menu

`application_launcher=auto|internal|external|disabled` controls the leftmost
block. `auto` prefers the internal GIO catalog when GIO, `xkbcommon-x11`, and
the native popup are available, then uses the optional argv-based `launcher=`
specification. The catalog reloads whenever it opens, honors `GAppInfo` menu
visibility, and launches only a selected registered desktop ID. Unicode
case-folded token search ranks name prefixes before word prefixes and other
substrings with stable alphabetical ordering. Arrow, Page Up/Down, Home/End,
Enter, Escape, Backspace, left click, and wheel navigation are supported.

`power_menu_mode=auto|internal|external|disabled` controls the rightmost block.
The internal menu queries `org.freedesktop.login1.Manager` over D-Bus and shows
only actions reported as `yes` or `challenge`. Lock is immediate; sleep,
hibernate, hybrid sleep, suspend-then-hibernate, reboot, and poweroff open a
confirmation popup with Cancel selected. If Sliverbar's inhibitor is active,
confirmation releases it before a sleep request and restores it if the request
fails. A generic logout entry is intentionally not guessed because safe logout
is session- or WM-specific.

`power_actions=` is an ordered comma-separated subset of `lock`, `suspend`,
`hibernate`, `suspend_then_hibernate`, `hybrid_sleep`, `reboot`, and
`poweroff`; capability detection may still hide entries. `power_confirm=`
selects which allowed actions require confirmation and defaults to every action
except lock. Lock appears only when a known session ScreenSaver service is
registered; the logind lock signal alone is not treated as proof that a locker
exists.

### Battery power profiles

When `power-profiles-daemon` is available on the system D-Bus and the native
popup is compiled in, left-clicking the battery block opens the profiles
reported by `net.hadess.PowerProfiles`. The active profile is marked, and only
an identifier currently offered by the service can be selected. Sliverbar sets
`ActiveProfile` through D-Bus with interactive authorization enabled and reads
the property back before treating the change as successful.

The integration is automatic and optional. Without the service, GIO, or the
native popup, the battery block remains visible but has no profile click action.
Sliverbar never writes power policy directly to `/sys` and never invokes
`sudo`. Do not run `power-profiles-daemon` alongside another power-policy
manager such as TLP or TuneD unless that combination is explicitly supported
by the installed tools. Command backends for those managers are not guessed.

When a battery is present, right-clicking the battery block toggles a native
details popup anchored to that block. It shows only values exposed by the
kernel: charge level, status, current charge, full charge capacity, design
capacity, calculated battery health, and current. Missing attributes are
omitted, and systems without a battery do not expose the right-click action.

`--diagnose` reports `power_profiles.backend`, `power_profiles.active`, the
number of offered profiles, and each profile identifier.

### Weather, timer, and inhibitor

Repeat `weather_location=safe-id|Display name|service query` for up to 4
locations and set `weather_default=safe-id`. Legacy `location=query` remains a
single-location form. A fifth location and empty, duplicate, overlong, or unsafe
IDs fail `--check-config`. Selection state is stored below
`$XDG_STATE_HOME/sliverbar`; JSON caches are isolated by safe ID below
`$XDG_CACHE_HOME/sliverbar/weather`.

`weather_interval` is specified in seconds and is limited to 1800–14400 seconds
(30–240 minutes). Values below or above that range are clamped to the nearest
limit and produce a warning in the log; for example, 900 seconds becomes 1800
seconds and 42000 seconds becomes 14400 seconds.

`language=auto` selects German when the system language is German and English
for every other system language. The same selection is used for Sliverbar's
menus, confirmations, notifications, clock abbreviations, and weather service.
Explicit `language=de` and `language=en` overrides remain available.

Weather mouse bindings are: left opens the native location list when multiple
locations exist, middle refreshes immediately, and right toggles a native
three-day forecast anchored below the weather block. The forecast shows 06,
09, 12, 15, 18, and 21 in Cairo-drawn time fields, followed by a weather glyph,
temperature, and rain probability for every time. Each localized day heading
also shows its minimum and maximum temperature. Missing values remain visible
as dashes, and an unavailable icon font falls back to monochrome Unicode
symbols. The popup header shows the location on the left and the cache update
time on the right; older data also includes its date. Existing cache data for a
newly selected location appears before its asynchronous refresh; an open
forecast redraws both its values and update time when refreshed data is
published.

Sliverbar no longer downloads or caches a forecast PNG and does not launch an
external image viewer for weather. The legacy `weather_image=` key is accepted
for compatibility, ignored, and reported as deprecated.

If a configured weather location cannot be fetched, the weather block stays
visible and shows placeholder values instead of disappearing. That keeps the
location picker and refresh action reachable even after a failed query.

The timer block immediately before the coffee-cup inhibitor uses the mouse
wheel to add or remove one minute while it is being set. Left click starts the
countdown; subsequent left clicks pause and resume it. Wheel input is ignored
after the first start. Right click resets a set, running, or paused timer to
zero without playing a sound or sending an elapsed notification. The idle
timer uses Nerd Font codepoint `U+F0020` in `color_clock`; a timer that is set
but not started uses `U+F0021`. While the countdown runs, the eight glyphs
`U+F0A9E` through `U+F0AA5` advance every 125 milliseconds and repeat once per
second. Starting or resuming begins with `U+F0A9E`; a paused timer uses
`U+F068E`. Set, running, and paused timers show their remaining whole minutes
to the left in `color_urgent`. After a manual reset, `U+F0023` is shown for 1.5
seconds unless the timer is wound again first. A right click on an already idle
timer does not show reset feedback.

`timer_sound` selects the audio file played after a natural expiry and defaults
to `/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga`. Sliverbar
uses the first available backend from `pw-play`, `paplay`,
`canberra-gtk-play`, and `aplay`. It also sends localized start and elapsed
notifications when `notify-send` is available. A missing sound file, playback
backend, or notification program does not affect the countdown or panel. The
elapsed glyph `U+F0022` remains visible while successful sound playback is
running. If playback cannot be started, it remains visible for 1.5 seconds
instead.

The clock glyph immediately to the left of the time follows the current local
hour, using the twelve Nerd Font clock glyphs `U+F144B` through `U+F1456` for 1
through 12 o'clock. Midnight and noon both use the 12 o'clock glyph. Without an
`icon_font`, the clock and timer use portable Unicode fallbacks.

The coffee-cup block immediately before weather toggles a real
`systemd-inhibit --what=sleep` lock. Inactive uses `color_free`, active uses
`color_warning`, and installations without an icon font use the Unicode coffee
cup. A desktop notification explains whether automatic standby and hibernation
are currently blocked or allowed. The block is hidden when `systemd-inhibit`
is unavailable. Its state is not restored after restarting Sliverbar.

### Network signal

For the active WLAN interface—preferably the one carrying the default
route—Sliverbar first reads kernel link quality from `/proc/net/wireless`. A
quality of 51/70 is rounded to 73%. If the driver exposes no valid value,
NetworkManager's normalized `nmcli` signal is the fallback. SSID data still
comes from NetworkManager when available. `--diagnose` reports interface,
backend, raw value, and rendered percentage.

The network block remains visible without an active connection. Ethernet uses
`󰈀`, unavailable Wi-Fi uses `󰤭`, and the four Wi-Fi signal ranges use `󰤟`,
`󰤢`, `󰤥`, and `󰤨`. A ten-pixel gap separates the Wi-Fi glyph from its
percentage. Right-clicking opens an anchored menu containing up to the three
strongest visible SSIDs that have saved NetworkManager profiles. A filled dot
`●` marks the active network, and selecting another entry activates its saved
profile. Two spaces separate each signal glyph from its SSID. The Wi-Fi menu
remains available while Ethernet is active. Opening the menu requests a scan
without blocking the panel and refreshes the saved-network results once per
second for up to five seconds.

## Feature mapping

| Bash component | C implementation |
| --- | --- |
| `start.sh`, `sighandler.sh` | supervisor, `poll`, `timerfd`, `signalfd` |
| `events.sh`, workspace block | EWMH backend with optional persistent `bspc subscribe report` backend |
| `xtmon.sh`, `title_server.sh` | native XCB property events |
| clock, CPU, battery, screencast | native `/proc`, `/sys`, time and XDG logic |
| volume and brightness | backend detection plus validated action protocol |
| network worker | `/sys/class/net`, optional nmcli query and monitor |
| weather worker | non-blocking child, atomic per-location JSON caches |
| trayer block | native XEmbed tray manager and direct child-window layout |
| launcher, power and terminal clicks | detached, argument-based exec |

The Bash directory is not read or executed by `sliverbar`.

Module blocks use configurable pixel padding (`block_padding`) and fixed pixel
gaps between glyphs and values rather than font-dependent spaces.

## Compatibility matrix

| Area | Status | Coverage |
| --- | --- | --- |
| Linux x86-64, Debian 13 container | tested | GCC, Clang, CLI-only, ASan/UBSan |
| X11, one RandR monitor | tested | Xvfb rendering and input smoke test |
| Multiple monitors and X screens | tested | Xvfb right-monitor selection and second X screen |
| bspwm | supported | optional report backend with EWMH fallback |
| Other EWMH window managers | supported | generic properties; WM-specific behavior is best effort |
| Non-EWMH window managers | best effort | independent modules work; workspaces/title may be absent |
| Fedora 42 and Arch | best effort | reproducible manual compatibility containers |
| Non-x86-64 | best effort | source build expected, not yet automated |

Optional module backends are not panel prerequisites: `wpctl`/`pactl`/`amixer`
for volume (with native `wpctl` preferred for mute on PipeWire), logind plus
`/sys/class/backlight` for hardware brightness with `xrandr` as a software
fallback, `nmcli` for NetworkManager details, `curl` for weather JSON refresh,
`notify-send` for notifications, and `systemd-inhibit` for the inhibitor.
Missing tools hide or reduce only the affected feature.
