# Sliverbar repository instructions

## Code

- Use C17 and preserve the existing CMake architecture.
- Keep the project buildable without XCB development headers.
- Format changed C files with clang-format.
- Run clang-tidy on translation units affected by C or header changes.
- Run CTest after each coherent C or header change before considering it locally
  validated.
- Keep generated files in out-of-source build directories.

## Repository search

- Run `./scripts/inspect.sh` first for a read-only repository overview; it
  includes the initial `rg --files` inventory.
- After that overview, prefer targeted `rg` searches and read only files
  relevant to the task.
- Exclude generated files, build directories, caches, dependencies, and `.git` unless explicitly relevant.

## Context7

- Wenn eine Aufgabe aktuelle Bibliotheks-, Framework-, SDK- oder
  API-Dokumentation erfordert, zuerst Context7 verwenden.
- Falls Context7 in der aktuellen Codex-Sitzung nicht verfügbar oder nicht
  ladbar ist, den Anwender ausdrücklich darauf hinweisen, dass Codex
  möglicherweise ohne den Context7-Wrapper gestartet wurde.
- Dabei folgenden Neustartbefehl nennen:

  ```bash
  cd /home/thomas/.config/bspwm/sliverbar
  ./scripts/codex-context7.sh
  ```

- Zum Fortsetzen einer bestehenden Sitzung den Wrapper so verwenden:

  ```bash
  ./scripts/codex-context7.sh resume <SESSION-ID>
  ```

- Nicht stillschweigend auf andere Dokumentationsquellen ausweichen, bevor der
  Anwender über das fehlende Context7 informiert wurde.

## Validation scripts

- `./scripts/quick-check.sh`: fast iterative checks.
- `./scripts/lint.sh`: local format and clang-tidy validation.
- `./scripts/test-local.sh`: local build and CTest.
- `./scripts/test.sh`: complete authoritative container validation.

## Staged validation

1. Run `./scripts/quick-check.sh` during iterative C or header development.
2. Run `./scripts/test-local.sh` and `./scripts/lint.sh` after each coherent C
   or header change.
3. For build-system, test-infrastructure, configuration, documentation, and
   packaging changes, follow the change-type matrix in `docs/development.md`.
4. Run `./scripts/test.sh` before committing C, header, build-system, or
   test-infrastructure changes and before pushing, releasing, or opening a PR,
   subject to the task-specific exceptions below.

The complete validation workflow is documented in [docs/development.md](docs/development.md).

## Deployment and packaging

- Follow [docs/deployment.md](docs/deployment.md) before replacing a live instance.
- Follow [docs/packaging.md](docs/packaging.md) before producing a distributable package.
- Do not start a full security scan without explicit user approval.

## GitHub CLI

- Run every `gh` command outside the sandbox.

## Versioning

- The single version source is `VERSION` and must use `MAJOR.MINOR.PATCH`.
- Increment `PATCH` for user-facing bug fixes and other backward-compatible
  product corrections.
- Increment `MINOR` for backward-compatible feature changes.
- Increment `MAJOR` for breaking or otherwise major changes, after agreeing the
  major-version change with the user.
- Do not increment the version solely for documentation, test, CI, or internal
  build-maintenance changes unless they change installed files, package
  metadata, compatibility, or runtime behavior.
- The program must support `sliverbar --version` and exit successfully.
- Keep version tests and documentation synchronized with `VERSION`.

## Live validation

- Obtain explicit user approval before replacing or testing a live instance.
- For runtime-affecting changes, exercise the changed behavior on the live
  system with the complete production configuration and record the result.
- For visually affecting changes, capture the current panel as a baseline, run
  the candidate with the complete production configuration, capture the result,
  and inspect both screenshots.
- Changes affecting both runtime behavior and visual output require both forms
  of validation.
- An applicable change remains incomplete until all required functional and
  visual live validations have passed.

## Definition of Done

A change is complete when:

- relevant files were located and inspected with targeted searches;
- `./scripts/quick-check.sh` passes when applicable; if it fails solely because
  of preserved unrelated changes, equivalent task-scoped checks pass and the
  limitation is reported;
- `./scripts/test-local.sh` and `./scripts/lint.sh` pass for C or header changes;
- `./scripts/test.sh` passes before committing C, header, build-system, or
  test-infrastructure changes and before push, release, or PR, subject to the
  task-specific exceptions below;
- task-scoped unstaged and staged changes pass `git diff --check` and
  `git diff --cached --check`, respectively;
- newly created task files are deliberately staged before the cached check or
  receive an equivalent whitespace-error check;
- unrelated full-worktree failures are reported but not modified;
- versioning remains correct;
- relevant documentation is updated;
- the task did not create, modify, stage, or remove unrelated files;
- runtime-affecting changes passed an approved functional live test;
- visually affecting changes passed an approved baseline/candidate screenshot
  comparison;
- live or deployment tests were performed only with explicit approval.

Documentation-only changes use the documentation checks in
`docs/development.md` and do not require the C test matrix or a live-system test
solely because documentation changed. Validate newly introduced or modified
executable runtime and deployment steps as applicable; any live execution still
requires explicit approval.

Packaging changes follow `docs/packaging.md`, including `./scripts/test.sh` and
package validation. They require a live-system test only when they affect
installation or runtime behavior on the target host.
