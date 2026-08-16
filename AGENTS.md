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

## Repository state preflight

- Before implementing, building, or deploying a product change, record the
  current commit, whether `HEAD` is attached to a branch, the local default
  branch tip, the upstream/default remote branch tip, and the commits between
  `HEAD` and those reference points.
- Start product work from the current upstream state. When a remote is
  configured and the task is not explicitly about a historical revision,
  fetch remote updates and update the intended working branch with
  `git pull --ff-only` before editing. Do not rely on previously fetched remote
  references when a current fetch is possible.
- If the checkout is dirty, detached, or cannot be fast-forwarded safely,
  preserve all existing and task changes and stop before pulling or switching
  destructively. Determine the correct current branch, inspect divergence, and
  carry the preserved changes onto the updated base without dropping or
  overwriting them.
- If network access or the remote update is unavailable, report that the remote
  state could not be verified and do not claim that the checkout is current.
- Treat an empty branch line from `./scripts/inspect.sh` as a detached-HEAD
  warning that requires explicit investigation; never continue from it as if
  it were an ordinary current branch.
- If `HEAD` is detached or behind the local default branch, stop product work
  before editing, building, or installing. Inspect the intervening commits and
  diffs and, when available, their pull-request descriptions, conversation
  comments, reviews, and resolved review threads for product requirements and
  regression fixes. Continue only after moving the work onto the intended
  current base or after the user explicitly approves a historical base.
- Do not assume that the checked-out files represent the latest product state
  merely because the worktree is clean or the configured remote is current.
- Preserve task changes while correcting the base. Do not discard, overwrite,
  or silently reimplement them when switching from a stale or detached base.
- Recheck the upstream relationship immediately before a final build, live
  installation, push, release, or pull request. If relevant upstream commits
  appeared during the task, inspect and integrate them, then repeat affected
  validation before continuing.

## Deployment preflight

- Before replacing a live binary, compare the checked-out `VERSION` with
  `sliverbar --version` from the installed binary and record both results.
- If the installed version is newer than the checkout, stop before building or
  installing and identify the missing commits or release. An equal version is
  not proof of equal source provenance; also verify that the candidate contains
  all runtime-affecting commits on the current default branch.
- Never replace a live instance with a candidate built from a detached, stale,
  or otherwise older source base unless the user explicitly requests that
  historical deployment.
- Before any live binary replacement, review all commits between the candidate
  base and the current local and remote default branches for already merged
  product corrections, even when the requested change appears unrelated to
  those commits.

## Context7

- When a task requires current library, framework, SDK, or API documentation,
  use Context7 first.
- If Context7 is unavailable or cannot be loaded in the current Codex session,
  explicitly tell the user that Codex may have been started without the
  Context7 wrapper.
- Provide the following restart command:

  ```bash
  cd $HOME/.config/bspwm/sliverbar
  ./scripts/codex-context7.sh
  ```

- To resume an existing session, use the wrapper as follows:

  ```bash
  ./scripts/codex-context7.sh resume <SESSION-ID>
  ```

- Do not silently fall back to other documentation sources before informing the
  user that Context7 is unavailable.

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
- Before replacing any compiled live binary, capture the current panel as a
  baseline, run the candidate with the complete production configuration,
  capture the result, and inspect both screenshots, even when the intended
  change is not visual.
- Compare screenshots semantically as well as visually. Dynamic literal values
  such as the clock, temperatures, percentages, counters, and network rates may
  change, but that allowance applies only to their values. It does not allow a
  field to be added, removed, duplicated, substituted, or given a different
  meaning.
- Treat any change in module order, field count, field meaning, glyph identity,
  glyph count, font selection, spacing, alignment, color role, or panel/popup
  geometry as a regression until the exact source change and intended product
  requirement are identified. Corrupt, fallback, missing, clipped, or replaced
  glyphs always fail visual validation.
- Do not explain away a structural screenshot difference as dynamic data. Every
  difference beyond literal value changes must be accounted for with source or
  product evidence before deployment may continue.
- Changes affecting both runtime behavior and visual output require both forms
  of validation.
- An applicable change remains incomplete until all required functional and
  visual live validations have passed.

## Definition of Done

A change is complete when:

- the repository-state preflight passed and the work is based on the intended
  current source history;
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
- every compiled live-binary replacement passed an approved baseline/candidate
  screenshot comparison with all non-literal differences accounted for;
- live or deployment tests were performed only with explicit approval.

Documentation-only changes use the documentation checks in
`docs/development.md` and do not require the C test matrix or a live-system test
solely because documentation changed. Validate newly introduced or modified
executable runtime and deployment steps as applicable; any live execution still
requires explicit approval.

Packaging changes follow `docs/packaging.md`, including `./scripts/test.sh` and
package validation. They require a live-system test only when they affect
installation or runtime behavior on the target host.
