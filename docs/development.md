# Development and validation

## Validation stages

Use the smallest suitable stage during development and the complete workflow
before publication:

```text
quick-check.sh → test-local.sh → test.sh
```

After the automated stages, every completed validation also requires an
explicitly approved live-system test and visual baseline/candidate screenshot
comparison as documented in [deployment.md](deployment.md).

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

### Authoritative container validation

```bash
./scripts/test.sh
```

This delegates to `scripts/container-build.sh` and is the required validation
before committing C changes, pushing, releasing, or opening a pull request. It
runs:

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
