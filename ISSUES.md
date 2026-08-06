# Review Issues

## High

- Runtime and IPC fallback paths under `/tmp` are predictable and are not
  ownership- or mode-checked before use.
  - Status: fixed on `issues/fix`.
  - Affected code: `src/main.c`, `src/control_ipc.c`, `src/util.c`.
  - Impact: if `XDG_RUNTIME_DIR` is missing, another local user can pre-create
    `/tmp/sliverbar-<uid>` and interfere with startup, lock-file creation, or
    control-socket behavior.
  - Suggested fix: verify existing runtime directories with `lstat`, require a
    directory owned by `getuid()` with mode no broader than `0700`, reject
    symlinks, and fail closed when the fallback is unsafe.

## Medium / High

- `configLoad()` silently processes overlong configuration lines as truncated
  1023-byte fragments.
  - Status: fixed on `issues/fix`.
  - Affected code: `src/config.c`.
  - Impact: long values can be accepted after truncation, and the remainder of
    the physical line can be parsed as another logical line. This can make
    invalid configuration appear valid or alter command/path/color values in a
    hard-to-debug way.
  - Suggested fix: reject any line that fills the buffer without a newline
    before EOF, then add tests for overlong valid-looking and invalid-looking
    lines.

## Medium

- Most `color_*` configuration keys are copied without validation, while only
  agenda colors use `validColor()`.
  - Status: fixed on `issues/fix`.
  - Affected code: `src/config.c`, `src/native_panel.c`, `src/native_popup.c`.
  - Impact: invalid colors are accepted by `--check-config` and later fall back
    during rendering. The native color parser is also lenient enough to accept
    malformed suffixes such as `#12345g`.
  - Suggested fix: apply the existing strict `#RRGGBB` validation to every
    color key and make the render-side parser reject non-hex trailing bytes.

- `runCapture()` has undefined/fragile behavior when called with `size == 0`
  or a null output pointer.
  - Status: fixed on `issues/fix`.
  - Affected code: `src/util.c`, public declaration in `include/panel.h`.
  - Impact: current callers appear to pass real buffers, but the helper is part
    of the project-wide utility API and performs pointer arithmetic on `out`
    even when the requested output size is zero.
  - Suggested fix: require `out != NULL && size > 0`, or internally drain into a
    fixed scratch buffer when the caller does not want captured output.

## Low / Medium

- `writeAtomic()` uses a predictable temporary filename and opens it without
  `O_EXCL`.
  - Status: fixed on `issues/fix`.
  - Affected code: `src/util.c`.
  - Impact: ordinary user-state writes are unlikely to be exploitable in normal
    setups, but the current pattern is unnecessarily race-prone and can interact
    badly with stale files or pre-created paths.
  - Suggested fix: create the temporary file with `mkstemp` or `openat` plus
    exclusive creation in the target directory, apply the requested mode, then
    `rename` it into place.

## Validation Notes

- Static review was based on `./scripts/inspect.sh` and targeted reads of the
  affected source files.
- `./scripts/quick-check.sh` was attempted during review but could not complete
  because `clang-format` was not installed in the local environment.
