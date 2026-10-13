# Code Signing Policy

This project's Windows binaries are signed free of charge by the
[SignPath Foundation](https://signpath.io/) for open source projects, under their standard
[terms](https://signpath.org/terms).

## What gets signed

The two Windows executables built by `.github/workflows/desktop-build.yml`'s `build` job from the
tip of `main`:

- `quarzlampe-tray.exe`
- `quarzlampe-console.exe`

Both are part of the Quarzlampe desktop tray app (`desktop/`), licensed GPL-3.0-or-later (see
[LICENSE](../LICENSE)) and documented in [desktop/PLAN.md](PLAN.md). Source code for everything
that gets signed is in this public repository, built unmodified by the workflow above - no
separately maintained or private fork is ever submitted for signing.

## How signing requests are made

Signing requests are submitted automatically by that same GitHub Actions workflow, immediately
after a release build of the two binaries above, using the
[`signpath/github-action-submit-signing-request`](https://github.com/signpath/github-action-submit-signing-request)
action. No manually-built or locally-produced binary is ever submitted - every signing request
corresponds to a specific commit on `main`, built on a GitHub-hosted runner from that commit's
checked-in source.

## Who can approve signing requests

Signing requests are reviewed and approved by the repository owner
([@Haschtl](https://github.com/Haschtl)), the sole maintainer of this project, via the SignPath
dashboard.

## Privacy

No personal data is collected beyond what SignPath Foundation's own
[privacy policy](https://signpath.org/privacy-policy) covers for the signing process itself.
