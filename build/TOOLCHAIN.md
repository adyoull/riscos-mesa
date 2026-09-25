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
