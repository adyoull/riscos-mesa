# Making a release

Releases are numbered after the Mesa version they contain: `20.3.5`, then
`20.3.5-2` for the next build of it, and so on. Each is a git tag
(`v20.3.5-N`) and a GitHub release with the built files attached. Big
binaries go on the release, never into git.

## 1. Check

- **Host tests:** `tests/run-all.sh` with `ARM=1` (adds Mesa's rendering
  checks on an emulated Cortex-A8). GitHub runs the rest on every push.
- **Khronos dEQP-EGL**, after changes to `egl/` or `patches/mesa`:
  `tests/host-harness/deqp/README.md`. The result should match
  `expected.txt` (update it only for improvements).
- **SDL overlay:** `tools/sdl-overlay-regen.sh --check`.
- **On a Pi:** the tests zip's Obey files for whatever changed (its
  ReadMe lists them). Record what was and wasn't tried: the release notes
  say which new things haven't been run on a Pi.

## 2. Write it up

- **`CHANGELOG.md`:** rename "(in development)" to the release's heading
  (`## 20.3.5-N: a few words`), with a paragraph on what it brings and
  short bullets. Longer notes go in the GitHub release.
- **Version mentions:** `docs/EGL-GUIDE.md` (its second line and "Where
  it comes from"). The tests ReadMe's first line is stamped by
  `build/package.sh`.
- **Release notes** for GitHub (Markdown): what's new, what hasn't been
  tried on a Pi, what to download. `dist/` holds earlier ones as models
  (it isn't in git).

## 3. Build

In riscos-crossdev's image (the toolchain `.riscos-deps` names), as the
GitHub build does:

    docker run --rm -v "$PWD":/src ghcr.io/adyoull/riscos-crossdev:1.8.1 build/ci.sh 20.3.5-N

or with a toolchain of your own:

    export GCCSDK_ENV=/path/to/gccsdk/env
    build/build-all.sh 20.3.5-N

This builds everything in order (logs in `stage/logs/`) and packages
`dist/` (`build/ci.sh` then runs riscos-crossdev's checks on the test
programs):

| File | Contents |
| --- | --- |
| `riscos-mesa-devkit-VERSION.tgz` | libraries, headers, beginner's guide, examples, docs |
| `riscos-mesa-examples-VERSION.zip` | the devkit's examples, built |
| `riscos-mesa-tests-VERSION.zip` | test programs and Obey files |
| `riscos-mesa-hello_pi-VERSION.zip` | the Pi's hello_triangle and friends |
| `riscos-mesa-ports-VERSION.zip` | Mesa's EGL demos, SDL's GL tests |
| `riscos-mesa-glut-VERSION.zip` | freeglut's demos |

The zips carry RISC OS filetypes (`tools/mkrozip.py`); unzip them on
RISC OS with SparkFS or `unzip`.

## 4. Tag and publish

    git commit -m "Release 20.3.5-N"          # the CHANGELOG and version changes
    git tag -a v20.3.5-N -m "riscos-mesa 20.3.5-N"
    git push origin main v20.3.5-N
    gh release create v20.3.5-N --latest --title "riscos-mesa 20.3.5-N: ..." \
        --notes-file RELEASE-NOTES-20.3.5-N.md dist/*-20.3.5-N.*

The devkit must keep its name, `riscos-mesa-devkit-20.3.5-N.tgz` on tag
`v20.3.5-N`: riscos-crossdev's `toolchain/deps-hook.sh` downloads it by
that name when its daily workflow sees the release. Only releases marked
"latest" are seen, so publish test builds and release candidates with
`--prerelease`.

Then start a new `## 20.3.5-(N+1) (in development)` section at the top of
`CHANGELOG.md` for the next changes.

## The GitHub build and dependency updates

`.github/workflows/riscos-build.yml` runs `build/ci.sh` in riscos-crossdev's
image on every push and pull request and keeps the zips and devkit as a
download on the run's page. `riscos-deps-update.yml` runs daily: when the
riscos-crossdev release named in `.riscos-deps` has a newer one, it builds
with it and opens a pull request saying whether that passed. Nothing is
merged or released by itself. Both need, in Settings > Actions > General,
"Workflow permissions: Read and write" and "Allow GitHub Actions to create
and approve pull requests" (riscos-crossdev's `docs/CI.md`).

riscos-crossdev in turn pins this repository's devkit, so a riscos-mesa
release gives crossdev a pull request, and the crossdev release after it
gives riscos-mesa one. While crossdev's toolchain hasn't changed, that one
changes nothing here: merge or close it.

## 5. Tell people

A short post on the ROOL forum: what's new, what to download, what needs
trying on real hardware.
