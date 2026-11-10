#!/bin/bash
# SDL 2 ($SDL_V in env.sh) + the RISC OS overlay (patches/sdl2/*.p, the authoritative
# copy; riscos-openttd takes its copy from here with
# tools/sdl-overlay-export.sh). The same .p files drop into the GCCSDK
# autobuilder libsdl2 recipe. We build WITH --enable-video-riscos-osmesa
# (GL via OSMesa); riscos-openttd builds the same files without it (no GL).
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
# The overlay's fingerprint: tells whether patches/sdl2 changed since the
# tree was made.
overlay_sum() { cat "$HERE"/patches/sdl2/*.p | sha256sum | cut -d' ' -f1; }
if [ ! -d SDL-release-$SDL_V ]; then
  fetch_verified "$SDL_URL" "$SDL_SHA256" SDL-$SDL_V.tgz
  tar xzf SDL-$SDL_V.tgz
  # Every .p must apply: a failure stops the build (inside a loop on the
  # left of &&, set -e alone wouldn't catch it)
  (cd SDL-release-$SDL_V && for p in "$HERE"/patches/sdl2/*.p; do
     patch -s -p0 < "$p" || { echo "does not apply: $(basename "$p")" >&2; exit 1; }
   done && ./autogen.sh)
  overlay_sum > SDL-release-$SDL_V/.overlay-sum
elif [ "$(cat SDL-release-$SDL_V/.overlay-sum 2>/dev/null)" != "$(overlay_sum)" ]; then
  # patches/sdl2 changed (a git pull, say) since this tree was made. Edits
  # made in the tree itself are fine (that's where the overlay is edited),
  # but the tree must now match the .p files, or it would build old code.
  if "$HERE"/tools/sdl-overlay-regen.sh --check; then
    overlay_sum > SDL-release-$SDL_V/.overlay-sum
  else
    echo "src/SDL-release-$SDL_V doesn't match patches/sdl2: remove it to build the"
    echo "overlay as it is now, or run tools/sdl-overlay-regen.sh to keep the tree's edits."
    exit 1
  fi
fi
[ -n "${SOURCES_ONLY:-}" ] && exit 0      # tests/host-setup.sh: the patched source is all it needs
fresh_build_dir SDL-release-$SDL_V/build-ro$RO_SUFFIX && cd SDL-release-$SDL_V/build-ro$RO_SUFFIX
# -I egl/include: SDL's GL windows are EGL window surfaces (programs then
# link -lEGL too: -lSDL2 -lGLU -lEGL -lOSMesa ...)
CFLAGS="$RO_CFLAGS -I$HERE/egl/include -I$STAGE/include" LDFLAGS="-L$STAGE/lib" \
  ../configure --host=$HOST --prefix="$STAGE" --disable-shared --enable-static \
  --enable-video-riscos-osmesa --disable-video-opengles --disable-video-rpi \
  --enable-arm-simd --enable-arm-neon
grep -q "define SDL_VIDEO_OPENGL_OSMESA 1" include/SDL_config.h || { echo "OSMesa GL NOT enabled"; exit 1; }
# SDL's ARM SIMD and NEON blitters: SDL checks the CPU at run time, so a
# machine without NEON uses the SIMD or C ones
for b in SDL_ARM_SIMD_BLITTERS SDL_ARM_NEON_BLITTERS; do
  grep -q "define $b 1" include/SDL_config.h || { echo "$b NOT enabled"; exit 1; }
done
grep -q "define SDL_VIDEO_RENDER_OGL 1" include/SDL_config.h && { echo "SDL GL renderer enabled - must stay off"; exit 1; }
libtool_ignore_toolchain_la
make -j"$(nproc)" && make install
# sem_timedwait: UnixLib before 5.0.3.6 has it only as a stub that always
# fails (ENOSYS), marked __stub_sem_timedwait in unixlib/stubs.h. With that
# UnixLib SDL must use its own timed wait (sdl2-configure.ac.host.p sees the
# marker), or SDL_SemWaitTimeout fails at once and the SDL_AddTimer thread
# spins. From 5.0.3.6 it's real, and SDL should use it: no polling.
# (each reads the whole output before grep: with pipefail, grep -q stopping
# early would make the command on the left fail)
sdl_uses_sem_timedwait() {
  local syms
  syms=$(arm-riscos-gnueabihf-nm "$STAGE/lib/libSDL2.a" 2>/dev/null)
  grep -q " U sem_timedwait$" <<< "$syms"
}
unixlib_sem_timedwait_is_stub() {
  local out
  out=$(printf '#include <semaphore.h>\n#ifdef __stub_sem_timedwait\nstub\n#endif\n' |
        arm-riscos-gnueabihf-gcc -E -P -)
  grep -q '^stub$' <<< "$out"
}
if unixlib_sem_timedwait_is_stub; then
  sdl_uses_sem_timedwait && { echo "libSDL2.a calls UnixLib's sem_timedwait stub"; exit 1; }
  echo "note: this UnixLib's sem_timedwait is a stub; SDL uses its polling wait (UnixLib 5.0.3.6 or later has the real one)"
else
  sdl_uses_sem_timedwait || { echo "UnixLib has sem_timedwait but libSDL2.a doesn't use it"; exit 1; }
fi
