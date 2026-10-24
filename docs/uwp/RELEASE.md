# UWP / Xbox release process

This runbook defines how to version, validate, and publish the Xbox / UWP package. The release workflow deliberately
does not publish a tag-triggered release automatically: it creates a draft so the exact package can be tested on an
Xbox before publication.

## Version scheme

Ymir's upstream version and the port's package revision are separate values:

| Value | Example | Meaning |
|---|---|---|
| Ymir version | `0.4.2` | Upstream emulator version from `project(Ymir VERSION ...)` |
| UWP package version | `0.4.2.1` | Four-part Windows package identity and release artifact version |
| Port revision | `1` | Fourth package component; shown in the Xbox shell and About page |

Official release tags use `uwp-v<Ymir-version>.<port-revision>`, for example `uwp-v0.4.2.1`. Start the first public
revision at `1`; increment it for each subsequent UWP release based on the same Ymir version. The CI rejects tags
whose first three components do not match the checked-out Ymir version. Windows package components must each fit in
the range 0–65535.

Untagged CI builds use the GitHub Actions run number as the fourth component. Local packages default to revision `0`;
they are development/test packages, not official releases. `build.ps1 -Configuration Release` disables Ymir's
development-build marker so the app reports the upstream version without `-dev`.

## Release sequence

1. Build and package locally with `pwsh scripts/uwp/build.ps1 -Configuration Release` and
   `pwsh scripts/uwp/package.ps1 -Configuration Release`.
2. Install and validate the candidate on the Xbox using the checklist in [TESTING.md](TESTING.md). The agent must not
   deploy or launch the app; the user performs this step in Visual Studio / Xbox Dev Mode.
3. Push the release tag, e.g. `uwp-v0.4.2.1`. The workflow validates the version, builds the package, uploads its ZIP
   and symbols, and creates a **draft GitHub Release**.
4. Download and test the ZIP attached to that draft. Publish the draft manually only after the exact attached package
   passes the Xbox checks. Do not rebuild between validation and publication.
5. If validation fails, fix the issue and use a new port revision for the next candidate. Do not reuse a published tag
   or package version.

Manual `workflow_dispatch` with `release: true` remains available for prerelease builds. It creates a prerelease rather
than a public stable release.

## Signing identity

The current default is CMake's test certificate (`CN=CMake Test Cert`), which is included in the distributable for
sideloading. The package family identity depends on both the manifest name and publisher. Changing the publisher later
creates a different package identity, so decide whether to retain this publisher or switch to a permanent certificate
before users accumulate installs and data. Never commit a private `.pfx` to the repository; CI signing secrets require
a separate setup.

## Workflow permissions and artifacts

The build job has read-only repository permissions. Only the release job receives `contents: write`; it downloads the
ZIP and symbols from the successful build job and attaches those exact artifacts to the draft/prerelease. Tag builds
are not public until someone publishes the draft.
