## Requirements

# Code

- Use C17.
- Format changed C files with clang-format.
- Run clang-tidy on changed translation units.
- Run CTest after every code change.
- Keep the project buildable without XCB development headers.

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
