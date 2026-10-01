# Building GCCSDK GCC 10.2.0 (arm-riscos-gnueabihf) from source

You only need this if you don't already have a GCCSDK GCC 10 install (e.g. the
OpenTTD buildkit). It follows GCCSDK's autobuilder recipe
`autobuilder/develop/gcc/setvars` with two deviations, both marked below.

1. binutils 2.30 + GCCSDK's `.pp` patches and RISC OS emulation files, then
   `configure --target=arm-riscos-gnueabihf --disable-nls --disable-werror`.
2. GCC 10.2.0 source with the files `copy_link_gcc` links in (the
   `gcc.config.arm.*`, `libgcc.config.arm.*`, `libstdc++-v3.config.os.riscos.*`
   files, UnixLib as `libunixlib/`, ld-riscos), then `autogen Makefile.def`,
   `autoconf2.69`, `reconf-libunixlib`, `reconf-libstdc++`.
3. **Deviation 1: configure-fooling stubs.** The recipe copies libc.a and
   libpthread.a from an existing GCCSDK 4.7.4 install so UnixLib's configure
   can run link tests. Without that install, put stubs in
   `$PREFIX/arm-riscos-gnueabihf/lib/` **before** building:
       printf '.global _start\n_start:\n bx lr\n' | as -o crt0.o
       empty libunixlib.a, libc.a, libpthread.a (ar rc of an empty .o)
       libunixlib.so = ld -m armelf_riscos_eabi -shared of an empty .o
   The shared stub matters: binutils 2.30's RISC OS code crashes (SIGSEGV
   in elf32_arm_size_dynamic_sections, NULL .riscos.abi.version section)
   when a dynamic link has no shared inputs. `make install` overwrites
   crt0.o and libunixlib.* with the real ones; the empty libc.a /
   libpthread.a stay, which is harmless (UnixLib provides both).
4. Export the recipe's cache overrides: `ac_cv_func_shl_load=no
   ac_cv_lib_dld_shl_load=no ac_cv_func_dlopen=yes glibcxx_cv_c99_math_tr1=yes`.
5. configure exactly as `build_cross_compiler` in setvars, languages c,c++.
6. **Deviation 2 (speed only):** `make CFLAGS=-O1 CXXFLAGS=-O1` for the
   host-side compiler build. Target libraries are unaffected.
7. `make install`. Smoke test: static C and C++ hello-worlds link.

On a 1-CPU container this took about 30 minutes after the source was ready.
The ld-riscos dynamic linker and the native (runs-on-RISC-OS) compiler steps
of the recipe were skipped; static linking doesn't need them.

## UnixLib 5.0.3.1-rc8 (riscos-unixlib)

riscos-mesa's release programs (tests, ports, examples) are linked with
**UnixLib 5.0.3.1-rc8** from github.com/adyoull/riscos-unixlib, a
pre-release: GCCSDK's UnixLib with the fixes the ports needed (5.0.1
until 20.3.5-10). riscos-unixlib is an unofficial fork, not made or
supported by the GCCSDK developers. The fixes that matter here:

- the **pthread ticker fix**: the thread switcher's code runs from the
  PThreadTicker module (or a copy in the RMA), so a threaded program (SDL
  sound, OpenAL) no longer crashes other tasks while it multitasks;
- `wctype()` and friends implemented: without them any C++ program whose
  `std::locale` set-up runs aborts at start with "wctype not implemented";
- sub-centisecond `clock_gettime(CLOCK_MONOTONIC)` and `nanosleep`;
- `sched_get_priority_min/max`, `fdatasync`; sound fixes for `/dev/dsp`;
- 5.0.2: files over 2GB (up to 4GB-1) with `-D_FILE_OFFSET_BITS=64`;
  nothing changes for programs built without it;
- 5.0.3: `ctime()`/`asctime()` returned a bad pointer, `read()` into an
  untouched stack buffer could stop a program with "EMT trap", and no
  build paths in the library. Same exported symbols as 5.0.2;
- 5.0.3.1 (pre-releases rc1 to rc8): fixes from a review of the fork
  (`fork`/`vfork` children no longer tear down their parent's thread
  timer, sound or stack; long sleeps; the monotonic clock), a heap past
  128 MB, `fork()` in EABI programs, `_exit(n)` exits with code n. It
  comes with PThreadTicker 0.02.

riscos-mesa's own libraries (`libOSMesa.a` etc.) don't contain UnixLib, so
they work with any UnixLib; it's the programs linked with them that need
5.0.1 or later (5.0.3, or 5.0.3.1 once it's released, recommended). Either:

- patch the GCCSDK source before building the toolchain
  (`patch -d riscos-gccsdk -p1 < patches/unixlib-riscos.diff` from that
  repository), or
- replace an installed toolchain's library: copy the release's
  `libunixlib.a` over the one `arm-riscos-gnueabihf-gcc
  -print-file-name=libunixlib.a` names (keep the old one), and the
  repository's `libunixlib/include/sched.h`, `unistd.h`, `sys/stat.h` and
  `sys/mman.h` into the `include/` directory next to that `lib/` (check
  the release's `SHA256SUMS` first); or run `make sources install`
  in that repository, which does the same from source.

Check: `arm-riscos-gnueabihf-nm <that libunixlib.a> | grep
sched_get_priority_min` prints a `T` line. Then relink your programs, and
ship the **PThreadTicker** module (`devkit/riscos/PThrTicker`) with any
threaded one, loaded from `!Run` with
`RMEnsure PThreadTicker 0.01 RMLoad <App$Dir>.PThrTicker`.
