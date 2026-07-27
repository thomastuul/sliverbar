# Development and validation

## Validation stages

Use the smallest suitable stage during development and the complete workflow
before publication:

```text
quick-check.sh → test-local.sh + lint.sh → test.sh
```

After the automated stages, runtime-affecting changes require an explicitly
approved functional live-system test. Visually affecting changes require an
approved visual baseline/candidate screenshot comparison. Changes affecting both
areas require both forms of validation as documented in
[deployment.md](deployment.md).

### Fast iterative checks

```bash
./scripts/quick-check.sh
```

This runs the read-only repository overview, `git diff --check`, and the local
clang-format check.

### Local build and tests

```bash
./scripts/test-local.sh
```

This uses the `local` CMake preset and writes to `build/local-host`. The preset
uses `RelWithDebInfo`, disables optional EDS support for host portability, and
keeps native XCB enabled when the host provides its development headers. If XCB
is unavailable, CMake builds the CLI-only fallback.

The local lint entry point is:

```bash
./scripts/lint.sh
```

It runs clang-format and clang-tidy against the local compile database.

Run `test-local.sh` before `lint.sh` after CMake, C, or header changes so that the
compile database is regenerated before clang-tidy reads it.

### Authoritative container validation

```bash
./scripts/test.sh
```

This delegates to `scripts/container-build.sh` and is the required validation
before committing C, header, build-system, or test-infrastructure changes and
before pushing, releasing, or opening a pull request, subject to the documented
task-specific exceptions. It runs:

- clang-format validation;
- clang-tidy;
- native release compilation;
- CLI-only compilation without XCB;
- CTest;
- ASan/UBSan tests;
- automated X11 tests under Xvfb;
- multiple EDS/XCB configurations;
- the `sliverbar --version` check.

Use rootless Podman instead of Docker with:

```bash
CONTAINER_ENGINE=podman ./scripts/test.sh
```

## Container rules

- Use Docker or rootless Podman for reproducible builds and static analysis.
- Keep dependencies, container definitions, and build commands versioned.
- Build as an unprivileged user and preserve host ownership of generated files.
- Do not use privileged containers.
- Do not mount the Docker or Podman control socket.
- Do not mount host X11, D-Bus, audio sockets, or unrelated directories unless
  a specific integration test requires them.
- Keep host integration mounts read-only unless writes are required.
- Do not run the production panel inside the development container.
- Keep container-built releases compatible with the target host libc and runtime.

## Local build details

The local CMake preset is defined in `CMakePresets.json`:

```bash
cmake --preset local
cmake --build --preset local
ctest --preset local
```

Do not manually duplicate long CMake command lines in project instructions.

## Validation by change type

| Change type | Required validation before considering the change complete |
| --- | --- |
| C source or header | `quick-check.sh`, `test-local.sh`, `lint.sh`, then `test.sh` before commit or publication |
| CMake, presets, Dockerfiles, build or test infrastructure | Relevant syntax checks and affected build/test entry points, then `test.sh` before commit or publication |
| Runtime configuration or defaults | Relevant config tests and `--check-config`; add `test.sh` when compiled behavior or release output is affected |
| Documentation only | Task-scoped `git diff --check` and `git diff --cached --check`, including new files after deliberate staging or an equivalent check, plus available link, format, and documentation checks; validate modified executable runtime or deployment steps as applicable |
| Packaging | `test.sh` followed by the package workflow in [packaging.md](packaging.md) |

For shell-script changes, also follow the inherited repository rules for
`bash -n` and ShellCheck.

When the working tree already contains unrelated changes, scope path-based checks
and staging to the task files. Report failures caused by unrelated paths without
modifying those paths. If `quick-check.sh` fails solely because it inspects such
paths, run the equivalent checks on the task paths and report that limitation.
