#!/bin/bash
# The unattended build: what GitHub runs (.github/workflows/riscos-build.yml
# and riscos-deps-update.yml) in the riscos-crossdev container image, and
# what you can run there yourself:
#
#   docker run --rm -v "$PWD":/src ghcr.io/adyoull/riscos-crossdev:1.8.1 build/ci.sh [VERSION]
#
# From a clean checkout it builds everything (build/build-all.sh), makes
# the release zips and devkit in dist/ (named VERSION, default "ci"), and
# runs riscos-crossdev's checks on the test programs. Nothing has to be put
# in place by hand: the sources download by checksum (build/env.sh).
#
# The compiler is the image's: $RISCOS_TOOLCHAIN (GCCSDK GCC 10.2 with
# riscos-unixlib), unless GCCSDK_ENV says otherwise. Mesa's build needs
# meson and Mako, which the image doesn't have: they're installed into
# .ci-tools (pinned versions, no root needed) when they aren't already
# there. CI_FORCE_TOOLS=1 uses .ci-tools even then.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
V=${1:-ci}

: "${GCCSDK_ENV:=${RISCOS_TOOLCHAIN:-}}"
[ -n "$GCCSDK_ENV" ] && [ -x "$GCCSDK_ENV/bin/arm-riscos-gnueabihf-gcc" ] || {
  echo "no compiler: set RISCOS_TOOLCHAIN or GCCSDK_ENV (run in the riscos-crossdev image)" >&2
  exit 1
}
export GCCSDK_ENV

# meson and Mako for Mesa's build, at the versions it's been built with
MESON_V=0.63.3 MAKO_V=1.3.5 MARKUPSAFE_V=3.0.4
T=${CI_TOOLS:-$R/.ci-tools}
if [ -n "${CI_FORCE_TOOLS:-}" ] || ! command -v meson >/dev/null || ! python3 -c 'import mako' 2>/dev/null; then
  if [ ! -f "$T/.done-$MESON_V-$MAKO_V" ]; then
    rm -rf "$T"
    pip3 install --quiet --disable-pip-version-check --break-system-packages --target "$T" \
      "meson==$MESON_V" "Mako==$MAKO_V" "MarkupSafe==$MARKUPSAFE_V"
    touch "$T/.done-$MESON_V-$MAKO_V"
  fi
  mkdir -p "$T/ci-bin"
  printf '#!/bin/sh\nexec python3 -m mesonbuild.mesonmain "$@"\n' > "$T/ci-bin/meson"
  chmod +x "$T/ci-bin/meson"
  export PYTHONPATH="$T${PYTHONPATH:+:$PYTHONPATH}" PATH="$T/ci-bin:$PATH"
fi
echo "compiler: $("$GCCSDK_ENV/bin/arm-riscos-gnueabihf-gcc" --version | head -1)"
echo "meson $(meson --version), Mako $(python3 -c 'import mako; print(mako.__version__)')"

"$R/build/build-all.sh" "$V"

# riscos-crossdev's checks (static, stack probes, FPU level, UnixLib) on
# the test programs, with the devkit's libraries for the FPU check
if [ -n "${RCD_DIR:-}" ] && [ -x "$RCD_DIR/checks/check-all.sh" ]; then
  S=${STAGE:-$R/stage}
  libs=$(ls "$S"/lib/lib{OSMesa,EGL,GLU,SDL2}.a 2>/dev/null || true)
  for p in glbench egltest glestest sdlgltest sdlfbtest; do
    f=$(ls "$S"/elf/$p,e1f 2>/dev/null | head -1)
    [ -n "$f" ] || { echo "no $p to check" >&2; exit 1; }
    "$RCD_DIR/checks/check-all.sh" "$f" $libs
  done
fi
echo "ci: built and checked; release files in dist/ (version $V)"
