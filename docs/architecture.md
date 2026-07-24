# Sliverbar architecture

## Overview

Sliverbar is a C17 X11 panel for Linux desktops with an EWMH-compatible window
manager.

The program consists of:

- the reusable `sliverbar_core` library;
- the `sliverbar` executable;
- the optional native XCB panel backend;
- optional Evolution Data Server agenda support;
- configuration, diagnostics, tests, and packaging layers.

## Build targets

CMake defines the following main targets:

```text
sliverbar_core
    ├── agenda
    ├── agenda_provider
    ├── app_launcher
    ├── config
    ├── inhibitor
    ├── modules
    ├── power_actions
    ├── timer
    ├── util
    └── weather_forecast

sliverbar
    ├── main
    ├── native_panel       optional XCB
    ├── native_popup       optional XCB
    ├── native_tray        optional XCB
    └── workspace_backend  optional XCB
```

`sliverbar_core` contains platform-independent and mostly reusable logic. The
`sliverbar` executable connects that logic to the native X11 runtime.

## Runtime flow

```text
configuration
      │
      ▼
main event loop
      │
      ├── timerfd
      ├── signalfd
      ├── X11/XCB events
      ├── bspwm workspace reports
      ├── NetworkManager updates
      └── supervised child processes
      │
      ▼
module state
      │
      ▼
panel layout and rendering
      │
      ├── Cairo
      ├── Pango
      └── XCB window
```

The main process uses one `poll(2)`-based event loop. It does not evaluate
shell code.

## Native panel

When the required XCB, Cairo, PangoCairo, and related development libraries are
available, CMake enables the native panel backend.

The native backend provides:

- the X11 dock window;
- EWMH strut and window properties;
- monitor and RandR handling;
- popup windows;
- XEmbed tray hosting;
- mouse and keyboard event handling;
- workspace backend integration.

When native dependencies are unavailable, the project still builds a CLI-only
fallback for configuration checks, diagnostics, version output, and tests.

## Optional integrations

### Evolution Data Server

EDS support is controlled by `SLIVERBAR_EDS`:

```text
AUTO  use EDS when available
ON    require EDS
OFF   disable EDS
```

The EDS provider supplies agenda and task data. The core remains buildable
without EDS.

### bspwm

The bspwm integration is optional. Workspace state is obtained through the
configured backend and is kept separate from generic panel rendering.

### External programs

Optional programs such as `pactl`, `amixer`, `xrandr`, `nmcli`, and desktop
applications are detected and executed through explicit argument vectors.
Commands are never passed through a shell evaluator.

## Configuration

Configuration is read from a key/value file. It controls:

- enabled modules;
- monitor selection;
- fonts;
- colors;
- workspace backend;
- application roles;
- weather locations;
- timers;
- launcher and power actions.

Configuration validation is performed before runtime startup with:

```bash
sliverbar --check-config --config PATH
```

## Process and safety model

Sliverbar:

- uses explicit `argv` arrays for child processes;
- does not evaluate shell command strings;
- supervises direct children;
- uses signal and timer file descriptors;
- publishes weather cache data atomically;
- uses `$XDG_RUNTIME_DIR/sliverbar` for its lock;
- keeps runtime detection and diagnostics in native C code.

## Testing architecture

Tests are built through CMake and executed with CTest.

The validation matrix covers:

- core unit tests;
- configuration validation;
- version output;
- diagnostics;
- weather JSON parsing;
- CLI-only builds;
- EDS-enabled builds;
- XCB-enabled builds;
- sanitizer builds;
- Xvfb native-panel tests;
- multi-monitor and workspace backends.

Validation is staged:

```text
quick-check.sh
      ↓
test-local.sh
      ↓
test.sh
```

The container workflow is authoritative before publication.

## Source layout

```text
src/         implementation
include/     public and internal headers
tests/       unit, integration, and Xvfb tests
scripts/     development, validation, and packaging entry points
config/      example runtime configuration
containers/  distribution-specific build definitions
man/         manual page template
docs/        development, deployment, packaging, and architecture docs
```

## Extension guidelines

When adding a module:

1. define its state and public interface;
2. keep platform-independent logic in `sliverbar_core` where possible;
3. connect it to the main event loop without introducing polling where an event
   source is available;
4. update configuration validation;
5. add unit or integration tests;
6. update the relevant documentation;
7. run the staged validation workflow.
