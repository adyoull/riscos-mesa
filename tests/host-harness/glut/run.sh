#!/bin/bash
# Host test of freeglut's RISC OS back end (glut/riscos): builds freeglut
# (patched, as build/build-freeglut.sh leaves it in $SRC/freeglut-3.8.0)
# against the fake RISC OS in ../egl, runs freeglut's own demos through
# portrun with scripted keys, clicks, drags, menus and the wheel, and checks
# what they report. Screens are saved as PPM files in $OUT.
#   M=<host Mesa 20.3.5 with patches/mesa, built as in ../egl/README.md>
#   GLU=<a host libGLU.a>   (e.g. glu-9.0.1's sources compiled with gcc)
#   STAGE=<riscos-mesa stage, for the GLES headers>
#   tests/host-harness/glut/run.sh
set -euo pipefail
R=$(cd "$(dirname "$0")/../../.." && pwd)
H=$R/tests/host-harness/egl
: "${SRC:=$R/src}" "${M:?host Mesa tree}" "${GLU:?host libGLU.a}" "${OUT:=/tmp/glut-host}"
FG=$SRC/freeglut-3.8.0; O=$M/build/src/mesa/drivers/osmesa; D=$FG/progs/demos
[ -f "$FG/src/riscos/fg_main_riscos.c" ] || { echo "run build/build-freeglut.sh first" >&2; exit 1; }
rm -rf "$FG/src/riscos"; cp -r "$R/glut/riscos" "$FG/src/riscos"   # the current back end
mkdir -p "$OUT/gl" "$OUT/gles"
: "${STAGE:=$R/stage}"
INC="-I$H/fake -I$R/egl/include -I$M/include -I$FG/include -I$FG/src -I$STAGE/include"   # GL/glu.h
DEFS="-DHAVE_SYS_TYPES_H -DHAVE_UNISTD_H -DHAVE_SYS_TIME_H -DHAVE_STDINT_H -DHAVE_FCNTL_H
 -DHAVE_ERRNO_H -DHAVE_VFPRINTF -DNEED_XPARSEGEOMETRY_IMPL -DFREEGLUT_PRINT_ERRORS"
CF="-no-pie -O1 -g -w -std=gnu99 -D__riscos__ $DEFS $INC"
GENERIC="callbacks cursor display ext font_data gamemode geometry gl2 init input_devices joystick
 main misc overlay spaceball state stroke_mono_roman stroke_roman structure teapot videoresize window"
lib() {   # dir extra-cflags extra-sources
  local d=$OUT/$1 s
  for s in $GENERIC; do gcc $CF $2 -c $FG/src/fg_$s.c -o $d/fg_$s.o; done
  for s in $3 x11/fg_glutfont_definitions_x11 util/xparsegeometry_repl riscos/fg_init_riscos \
           riscos/fg_main_riscos riscos/fg_window_riscos riscos/fg_state_riscos riscos/fg_menu_riscos; do
    gcc $CF $2 -c $FG/src/$s.c -o $d/$(basename $s).o
  done
  rm -f $d/libglut.a; ar rcs $d/libglut.a $d/*.o
}
lib gl "" "fg_font fg_menu"
lib gles "-DFREEGLUT_GLES -I${STAGE:-$R/stage}/include" "riscos/fg_gles_riscos"
gcc -no-pie -O1 -w -std=gnu99 -D__riscos__ -I$H/fake -I$R/egl/include -I$M/include -I$R/dispmanx \
    -c $R/egl/egl_riscos.c -o $OUT/egl_riscos.o
gcc -no-pie -O1 -w -I$H -I$H/fake -c $H/fake_riscos.c -o $OUT/fake_riscos.o
gcc -no-pie -O1 -w -I$H -I$H/fake -c $H/portrun.c -o $OUT/portrun.o
demo() {  # name sources...
  local n=$1; shift
  gcc -no-pie -O1 -w -std=gnu99 -D__riscos__ -Dmain=app_main $INC "$@" $OUT/egl_riscos.o \
      $OUT/fake_riscos.o $OUT/portrun.o $OUT/gl/libglut.a -o $OUT/$n -L$O -lOSMesa $GLU -lstdc++ \
      -lpthread -lm -Wl,-rpath,$O
}
fails=0
expect() {  # file pattern description
  if grep -q -- "$2" "$1"; then echo "ok    $3"; else echo "FAIL  $3"; fails=$((fails+1)); fi
}
cd $OUT
demo one $D/One/one.c
MENUS=1 FRAMES=10 KEYS=27,n,n,n,c10:10:2,s2,c275:20:1,s5:0,u PPM=one.ppm ./one </dev/null >one.txt 2>&1 || true
expect one.txt "current window is 1280x720" "game mode covers the screen"
expect one.txt "leaving gamemode" "Escape leaves game mode"
expect one.txt 'menu "one"' "Wimp menu built from a GLUT menu"
expect one.txt "menuID is 3" "Menu button opens the menu attached to GLUT_RIGHT_BUTTON; choice made"
expect one.txt "MenuStatus is 1 at (25,20)" "Adjust over a nested subwindow opens its menu (subwindow coordinates)"
expect one.txt "menuID is 1" "submenu choice"
demo callbackmaker $D/CallbackMaker/CallbackMaker.c
FRAMES=3 KEYS=97,0x181,0x19F,0x18C,0x1CC,0x1E,0x18B,c20:30,m40:50,u,w2,r1000x800,n PPM=cb.ppm \
  ./callbackmaker </dev/null >cb.txt 2>&1 || true
expect cb.txt "Keyboard Callback:  97" "key press"
expect cb.txt "Key Release Callback:  97" "key release"
expect cb.txt "Special Key Callback:  1 " "F1"
expect cb.txt "Special Key Callback:  104" "Page Up (Shift+Up without Shift)"
expect cb.txt "Special Key Callback:  100" "cursor left"
expect cb.txt "Special Key Callback:  12 " "F12"
expect cb.txt "Special Key Callback:  106" "Home"
expect cb.txt "Special Key Callback:  107" "Copy/End"
expect cb.txt "Mouse Click Callback:  0 0 20 30" "Select down"
expect cb.txt "Mouse Motion Callback:  40 50" "drag"
expect cb.txt "Mouse Click Callback:  0 1 40 50" "Select up"
expect cb.txt "Mouse Wheel Callback:  0 1" "scroll wheel"
expect cb.txt "Window 5 Reshape Callback:  500 400" "window resized by the user"
expect cb.txt "Position Callback:  0 300" "subwindow position"
demo subwin $D/subwin/subwin.c
FRAMES=5 KEYS=n PPM=subwin.ppm ./subwin </dev/null >subwin.txt 2>&1 || true
expect subwin.txt "app_main returned\|polls" "subwindows run"
cp $D/Fractals_random/fractals.dat .
demo fractrand $D/Fractals_random/fractals_random.c
FRAMES=20 KEYS=n PPM=fr.ppm ./fractrand </dev/null >fr.txt 2>&1 || true
expect fr.txt "plots [1-9][0-9]" "single-buffered window drawn from the idle callback is shown"
gcc -no-pie -O1 -w -std=gnu99 -D__riscos__ -DFREEGLUT_GLES -Dmain=app_main -I${STAGE:-$R/stage}/include $INC \
    $R/tests/host-harness/glut/es2tri.c $OUT/egl_riscos.o $OUT/fake_riscos.o $OUT/portrun.o \
    $OUT/gles/libglut.a -o $OUT/es2tri -L$O -lOSMesa -lpthread -lm -Wl,-rpath,$O
FRAMES=3 KEYS=n PPM=es2tri.ppm ./es2tri </dev/null >es2tri.txt 2>&1 || true
expect es2tri.txt "OpenGL ES 2.0" "OpenGL ES 2.0 build (libfreeglut-gles)"
echo "$fails failures"
[ $fails = 0 ]
