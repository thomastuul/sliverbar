# Sliverbar repository instructions

## Code

- Use C17 and preserve the existing CMake architecture.
- Keep the project buildable without XCB development headers.
- Format changed C files with clang-format.
- Run clang-tidy on changed translation units.
- Run CTest after every code change.
- Keep generated files in out-of-source build directories.

## Repository search

- Use `./scripts/inspect.sh` for a read-only repository overview before reading files.
- Use `rg --files` first to inspect the repository structure.
- Prefer targeted `rg` searches and read only files relevant to the task.
- Exclude generated files, build directories, caches, dependencies, and `.git` unless explicitly relevant.

## Validation scripts

- `./scripts/quick-check.sh`: fast iterative checks.
- `./scripts/lint.sh`: local format and clang-tidy validation.
- `./scripts/test-local.sh`: local build and CTest.
- `./scripts/test.sh`: complete authoritative container validation.

## Staged validation

1. Run `./scripts/quick-check.sh` during iterative development.
2. Run `./scripts/test-local.sh` after normal C changes.
3. Run `./scripts/test.sh` before committing, pushing, releasing, or opening a PR.

The complete validation workflow is documented in [docs/development.md](docs/development.md).

## Deployment and packaging

- Follow [docs/deployment.md](docs/deployment.md) before replacing a live instance.
- Follow [docs/packaging.md](docs/packaging.md) before producing a distributable package.
- Do not start a full security scan without explicit user approval.

## Versioning

- The single version source is `VERSION` and must use `MAJOR.MINOR.PATCH`.
- Increment `PATCH` for bug fixes and other backward-compatible corrections.
- Increment `MINOR` for backward-compatible feature changes.
- Increment `MAJOR` for breaking or otherwise major changes, after agreeing the
  major-version change with the user.
- The program must support `sliverbar --version` and exit successfully.
- Keep version tests and documentation synchronized with `VERSION`.

## Live validation

- Every completed Sliverbar validation must include a test of the candidate on
  the live system and a visual comparison using screenshots.
- Obtain explicit user approval before replacing or testing a live instance.
- Capture the current panel as a baseline, run the candidate with the complete
  production configuration, capture the result, and inspect both screenshots.
- A change that has not completed the approved live test and screenshot review
  remains incomplete.

## Definition of Done

A change is complete when:

- relevant files were located and inspected with targeted searches;
- `./scripts/quick-check.sh` passes when applicable;
- `./scripts/test-local.sh` passes for C changes;
- `./scripts/test.sh` passes before commit, push, release, or PR;
- `git diff --check` passes;
- versioning remains correct;
- relevant documentation is updated;
- no unrelated files are changed;
- live or deployment tests were performed only with explicit approval.

For documentation-only or packaging-only changes, apply the relevant checks from
the linked development, deployment, or packaging document instead of forcing the
full C test matrix when it is not applicable.
