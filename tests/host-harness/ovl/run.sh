#!/bin/bash
# Host test of tests/ovltest.c, the Pi VideoOverlay test program: each of
# its eight tests runs on the fake RISC OS with a fake VideoOverlay module
# (fake_ovl.c), with scripted keys, and the checks look at what it wrote
# to ovlresults and what the fake saw. It can't show what a real overlay
# looks like: that's what the Pi run is for. This checks the program
# itself: that it keeps to the VideoOverlay API (selectors with variables
# 0, 3 and 13, live IDs, map/unmap pairs, nothing mapped across Wimp_Poll
# where that isn't the test, everything destroyed), survives a mode
# change, honours plane strides, and writes YCbCr that converts back to
# the right colours for each matrix and range.
#   OUT=<scratch dir> tests/host-harness/ovl/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${OUT:=/tmp/riscos-mesa-ovl}"
OUT=$OUT "$HERE/build.sh"
cd "$OUT"
fails=0
expect() {   # description command...
  local d=$1; shift
  if "$@"; then echo "ok    $d"; else echo "FAIL  $d"; fails=$((fails+1)); fi
}
run() {      # test keys [env...]
  local t=$1 keys=$2; shift 2
  rm -f ovlresults
  env OVLTEST_VIRTUAL_MS=20 KEYS="$keys" "$@" ./ovltest "$t" > "$t.out" 2>&1 || true
  cp ovlresults "$t.results" 2>/dev/null || touch "$t.results"
}
clean() {    # test: finished, no protocol errors, nothing left alive
  grep -q "app_main returned" "$1.out" && ! grep -q "FAKE-ERROR" "$1.out" &&
    grep -q "T[0-9] done" "$1.results"
}

run vet n100
expect "T1 vet: runs cleanly"                  clean vet
expect "T1 vet: the full table (360 rows)"     test "$(grep -cE ' (yes|no): ' vet.results)" = 360
expect "T1 vet: sizes over 2048 refused"       grep -q "2560x1440  3     must   no:" vet.results
expect "T1 vet: BGR565 refused, RGB565 taken"  grep -q "BGR565          1280x720 x3: no" vet.results

run bw n100
expect "T2 bw: runs cleanly"                   clean bw
expect "T2 bw: 15 speed lines"                 test "$(grep -c 'MB/s best' bw.results)" = 15
expect "T2 bw: YV12 planes and copy timed"     grep -q "YV12 1920x1080 copy in:" bw.results

run cache n20,y,y,n,y,n20,y,y,y,y,n30 FAKE_OVL_ALLOW_MAPPED=1
expect "T3 cache: runs cleanly"                clean cache
expect "T3 cache: 8 answers recorded"          test "$(grep -c 'answer:' cache.results)" = 8

run map n1600,D,y,n30 FAKE_OVL_ALLOW_MAPPED=1
expect "T4 map: runs cleanly"                  clean map
expect "T4 map: 30 seconds, same addresses"    grep -q "buffer 2 mapped again: .*(same address)" map.results
expect "T4 map: map/unmap cost measured"       grep -q "MapBuffer + UnmapBuffer:" map.results

run tear n520,0,n520,1,n520,2,n520,0,n30
expect "T5 tear: runs cleanly"                 clean tear
expect "T5 tear: four runs with timings"       test "$(grep -c 'DisplayBuffer .* us (min/avg/max)' tear.results)" = 4
expect "T5 tear: a switch every frame"         grep -q "fake-ovl: .* 2004 displays" tear.out

run scale n20,D,2,n5,1,n5,m,n5,r1000x900,n5,f,D,n5,f,n5,f,n5,f,n5,q,n20
expect "T6 scale: runs cleanly"                clean scale
expect "T6 scale: follows a resize"            grep -q "area now 500x250 px" scale.results
expect "T6 scale: all four formats made"       test "$(grep -cE '^(TBGR32|YV12|YV16|NV12) .*640x360, Basic' scale.results)" = 5
expect "T6 scale: RedrawWindow in redraws"     grep -qE "fake-ovl: .* [1-9][0-9]* redraws" scale.out

run desk n100,D,M,n100,h,n20,h,n20,f,n20,f,n20,q,n20 FAKE_OVL_STALE_OK=1
expect "T7 desk: runs cleanly"                 clean desk
expect "T7 desk: old ID refused after a mode change" grep -q "old ID: error" desk.results
expect "T7 desk: overlay made again"           grep -q "Created again" desk.results
expect "T7 desk: H hides and shows it"         grep -q "Overlay shown again" desk.results
expect "T7 desk: F freezes and restarts it"  grep -q "Running again" desk.results
expect "T7 desk: window stack watched"         grep -q "Stack moves with no Open_Window_Request: 0" desk.results

run yuv n20,D,c,n5,c,t,n5,v,n5,v,n5,v,n5,v,q,n20 FAKE_OVL_BARS=1
expect "T8 yuv: runs cleanly"                  clean yuv
expect "T8 yuv: bars right in all four colour spaces" \
  test "$(grep -c 'bars read back correctly' yuv.out)" -ge 5
expect "T8 yuv: copy timed"                    grep -q "map + copy + unmap:" yuv.results

# as on the Pi: Vet always fails, and with Geminus the GPU has room for
# only two 1920x1080 buffers
run vet n100 FAKE_OVL_VET_BROKEN=1
expect "Pi's broken Vet: Create still finds the formats" grep -q "YV12 709 video  1280x720 x3: Basic" vet.results
expect "Pi's broken Vet: logged beside Create"  grep -q "1280x720   3     must   yes: .*GraphicsV call failed" vet.results
run tear n20,n520,0,n520,1,n30 FAKE_OVL_GPU_BYTES=20000000
expect "GPU full: 3-buffer runs refused up front" grep -q "Buffer 3 of 3 can't be mapped" tear.results
expect "GPU full: 2-buffer runs still happen"   test "$(grep -c 'DisplayBuffer .* us (min/avg/max)' tear.results)" = 2
run desk n100,q,n20 FAKE_OVL_GPU_BYTES=20000000
expect "GPU full: T7 falls back to 2 buffers"   grep -q "Using 2 buffers" desk.results

# as on the Pi: the old overlay survives a mode change; without destroying
# it, the third change ran out of GPU memory
run desk n50,M,n20,M,n20,M,n20,q,n20 FAKE_OVL_MODE_KEEPS=1 FAKE_OVL_GPU_BYTES=60000000
expect "mode change, old overlay kept: destroyed" test "$(grep -c 'Destroy with the old ID: ok' desk.results)" = 3
expect "mode change, old overlay kept: 3 buffers every time" test "$(grep -c "can't be mapped" desk.results)" = 0
expect "mode change, old overlay kept: nothing left over" clean desk

# auto-hide: a window in front hides the overlay, both modes
run desk n30,a,n10,B,n10,B,n10,a,n10,B,n10,B,n10,q,n20
expect "auto-hide: hides and shows again, twice" test "$(grep -c 'Auto-hide: hidden' desk.results)" = 2
expect "auto-hide: shown again each time"      test "$(grep -c 'Auto-hide: shown again' desk.results)" = 2
expect "auto-hide: runs cleanly"               clean desk

# "ovltest desk auto" (ovl-desk-auto): auto-hide on from the start
rm -f ovlresults
env OVLTEST_VIRTUAL_MS=20 KEYS=n20,B,n10,B,n10,q,n20 ./ovltest desk auto > deskauto.out 2>&1 || true
cp ovlresults deskauto.results
expect "desk auto: starts with auto-hide on"   grep -q "Auto-hide is on: when a window or menu overlaps" deskauto.results
expect "desk auto: hides and shows"            grep -q "Auto-hide: shown again" deskauto.results

run vet n20 FAKE_OVL_MISSING=1
expect "no VideoOverlay: says so and quits"    grep -q "VideoOverlay isn't loaded" vet.results

if [ $fails -eq 0 ]; then echo "ovltest host checks: ALL PASS"; else echo "ovltest host checks: $fails FAILED"; exit 1; fi
