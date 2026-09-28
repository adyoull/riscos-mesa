#!/bin/bash
# Host test of the devkit examples (devkit/examples): each one is built for
# Linux against the fake RISC OS in ../egl (the same egl_riscos.c, a host
# OSMesa) and run through portrun, which plays the desktop: it sends the
# redraws, null events and keys, then a close request, and saves the fake
# screen. The checks look at what reached the screen.
#   1-fullscreen, 2-window, 3-shaders: the triangle's three corner colours
#     on the dark blue background, and a clean exit;
#   5-glut: the orange teapot, Space and Escape handled;
#   6-sound: built against the host SDL and OpenAL of ../openal (run that
#     first), recorded with SDL's disk driver: three notes, left, middle,
#     right, at the right pitches.
# 4-sdl2 needs SDL itself on the fake RISC OS, so it's only cross-built
# (the SDL driver has its own tests in ../sdl-wimp and ../harness.c).
#
#   M=<host Mesa tree>  GLUT=<host libglut.a, from ../glut/run.sh>
#   GLU=<host libGLU.a>  AL=<host SDL + OpenAL prefix, from ../openal/run.sh>
#   tests/host-harness/examples/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
H=$R/tests/host-harness/egl
X=$R/devkit/examples
: "${M:?host Mesa tree}" "${OUT:=/tmp/riscos-mesa-examples}"
O=$M/build/src/mesa/drivers/osmesa
mkdir -p "$OUT"; cd "$OUT"
F="-no-pie -O1 -w -std=gnu99"
gcc $F -D__riscos__ -I$H/fake -I$R/egl/include -I$M/include -I$R/dispmanx -c $R/egl/egl_riscos.c -o egl_riscos.o
gcc $F -I$H -I$H/fake -c $H/fake_riscos.c -o fake_riscos.o
gcc $F -I$H -I$H/fake -c $H/portrun.c -o portrun.o
fails=0
expect() {   # description command...
  local d=$1; shift
  if "$@"; then echo "ok    $d"; else echo "FAIL  $d"; fails=$((fails+1)); fi
}
app() {      # name source extra-libs...
  local n=$1 s=$2; shift 2
  gcc $F -D__riscos__ -Dmain=app_main -I$H/fake -I$R/egl/include -I$M/include "$X/$s" \
      egl_riscos.o fake_riscos.o portrun.o -o "$n" "$@" -L$O -lOSMesa -lpthread -lm -Wl,-rpath,$O
}

app fullscreen 1-fullscreen/fullscreen.c
FRAMES=0 PPM=fullscreen.ppm ./fullscreen </dev/null > fullscreen.txt 2>&1 || true
expect "1-fullscreen: runs to the end"     grep -q "app_main returned" fullscreen.txt
expect "1-fullscreen: triangle on the screen" python3 "$HERE/colours.py" fullscreen.ppm triangle

app window 2-window/window.c
FRAMES=20 KEYS=n PPM=window.ppm ./window </dev/null > window.txt 2>&1 || true
expect "2-window: quits on the close request" grep -q "app_main returned" window.txt
expect "2-window: triangle in the window"   python3 "$HERE/colours.py" window.ppm triangle
expect "2-window: frames were drawn"        grep -qE "plots ([2-9][0-9]|1[0-9])" window.txt

app shaders 3-shaders/shaders.c
FRAMES=20 KEYS=n PPM=shaders.ppm ./shaders </dev/null > shaders.txt 2>&1 || true
expect "3-shaders: quits on the close request" grep -q "app_main returned" shaders.txt
expect "3-shaders: shaded triangle in the window" python3 "$HERE/colours.py" shaders.ppm triangle

if [ -n "${GLUT:-}" ] && [ -n "${GLU:-}" ]; then
  gcc $F -D__riscos__ -Dmain=app_main -I$H/fake -I$R/egl/include -I$M/include \
      -I$R/src/freeglut-3.8.0/include -I${STAGE:-$R/stage}/include "$X/5-glut/glut.c" \
      egl_riscos.o fake_riscos.o portrun.o -o teapot "$GLUT" "$GLU" -L$O -lOSMesa -lstdc++ -lpthread -lm -Wl,-rpath,$O
  FRAMES=10 KEYS=32,n,n,27 PPM=teapot.ppm ./teapot </dev/null > teapot.txt 2>&1 || true
  expect "5-glut: Escape leaves glutMainLoop" grep -q "app_main returned" teapot.txt
  expect "5-glut: lit orange teapot"          python3 "$HERE/colours.py" teapot.ppm teapot
else
  echo "skip  5-glut (set GLUT= and GLU=)"
fi

if [ -n "${AL:-}" ]; then
  gcc -O2 -I"$AL/include" -I"$AL/include/SDL2" "$X/6-sound/sound.c" -o tune \
      -L"$AL/lib" -lopenal -lSDL2 -lm -lpthread -ldl
  printf '[general]\nchannels = stereo\nsample-type = int16\nfrequency = 22050\n' > alsoft.conf
  rm -f tune.raw
  ALSOFT_CONF=alsoft.conf SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=tune.raw ./tune
  expect "6-sound: three notes, left to right" python3 "$HERE/tune.py" tune.raw
else
  echo "skip  6-sound (set AL= to ../openal's host prefix)"
fi
echo "$fails failures"
[ $fails = 0 ]
