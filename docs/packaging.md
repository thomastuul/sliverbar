# Packaging validation

## Before producing a package

- Run the complete validation workflow:

  ```bash
  ./scripts/test.sh
  ```

- Verify the installed binary.
- Verify configuration files and runtime-library requirements.
- Verify package contents and version metadata.
- Keep package generation integrated with the existing CMake/install metadata,
  such as CPack.

Use the existing scripts rather than duplicating package build commands:

```bash
./scripts/package-build.sh
```

On GitHub, keep packaging orchestration in workflow files and reuse the same
repository scripts:

- `.github/workflows/test.yml` runs `./scripts/test.sh` for push and pull
  request validation.
- `.github/workflows/release.yml` runs `SLIVERBAR_STATIC_REVIEWED=1
  ./scripts/package-build.sh` for tag builds and publishes the generated
  package files.

## Security review

Before producing a distributable package, perform a restricted, read-only
security review of the repository. Do not request Trusted Access for Cyber.
Limit the review to threat modeling and static source inspection.

Do not generate exploits or proof-of-concept attacks, perform offensive testing,
or access external systems. A full security scan is optional and requires
explicit user approval.

## Runtime compatibility

- Confirm that the container-built release is compatible with the target host's
  libc and runtime environment.
- Document required host runtime libraries for dynamically linked binaries.
- If host library installation must be avoided, provide an explicit packaging
  solution rather than assuming containerization removes runtime dependencies.
