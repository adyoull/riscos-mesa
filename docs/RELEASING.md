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

    export GCCSDK_ENV=/path/to/gccsdk/env
    build/build-all.sh 20.3.5-N

This builds everything in order (logs in `stage/logs/`) and packages
`dist/`:

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
    gh release create v20.3.5-N --title "riscos-mesa 20.3.5-N: ..." \
        --notes-file RELEASE-NOTES-20.3.5-N.md dist/*-20.3.5-N.*

Then start a new `## 20.3.5-(N+1) (in development)` section at the top of
`CHANGELOG.md` for the next changes.

## 5. Tell people

A short post on the ROOL forum: what's new, what to download, what needs
trying on real hardware.
