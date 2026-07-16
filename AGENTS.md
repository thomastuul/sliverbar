## Requirements

# Code

- Use C17.
- Format changed C files with clang-format.
- Run clang-tidy on changed translation units.
- Run CTest after every code change.
- Keep the project buildable without XCB development headers.

# Containerized Development

- Use Docker or rootless Podman for reproducible compilation, static analysis,
  sanitizer builds, and automated tests.
- Build dependencies may be installed inside the development container. This
  includes compilers, CMake, Ninja, clang-format, clang-tidy, Xvfb, development
  headers, and required build or runtime libraries.
- Do not install project build dependencies on the host when they can be kept
  inside the development container.
- Keep the container definition, build commands, and dependency versions in
  version-controlled project files.
- Use CMake presets or project scripts for container builds instead of
  duplicating long command lines in documentation or agent instructions.
- Build as an unprivileged user and ensure generated files are owned by the
  invoking host user.
- Use out-of-source build directories and never write generated build artifacts
  into `src`, `include`, or `tests`.
- Run clang-format, clang-tidy, CTest, and sanitizer tests inside the container
  before committing C code changes.
- Use Xvfb for automated X11 integration and rendering tests inside the
  container whenever possible.
- Do not run the production panel inside the development container. The final
  panel should run as a normal process in the user's graphical session.
- Do not use privileged containers.
- Do not mount the Docker or Podman control socket into the container.
- Do not mount the host X11 socket, D-Bus socket, audio sockets, or unrelated
  host directories unless a specific integration test requires that access.
- Keep any host integration mount read-only unless the test explicitly requires
  writes.
- A container-built release must remain compatible with the target host's libc
  and runtime environment.
- Document the host runtime libraries required by dynamically linked release
  binaries. If releases must avoid host library installation, provide an
  explicit packaging solution instead of assuming that containerized builds
  remove runtime dependencies.

# Versioning

The project must have a centrally managed version number.

- The version number must use Semantic Versioning:
  `MAJOR.MINOR.PATCH`, for example `1.4.2`.
- The version number must be defined in only one central location.
- The program must be able to print the version number from the command line.
- Supported option:

      program-name --version

- The output should contain only, or at least, the following:

      program-name 1.4.2

- `--version` must exit successfully with exit code 0.
- The versioning logic must not change normal program execution.
- Scattered or independently maintained duplicate version numbers must be avoided.
- Tests for the `--version` option must be added or updated.
- The documentation must mention the `--version` option.

## Implementation

Before making changes:

1. Determine the programming language, build system, and existing command-line parser.
2. Check whether a central version definition already exists.
3. Use the project's customary version source, for example:
   - `pyproject.toml`
   - `CMakeLists.txt`
   - `package.json`
   - Cargo metadata
   - a dedicated file such as `VERSION`
4. After making the change, run the existing tests and additionally invoke
   `program-name --version` directly.
