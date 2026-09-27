# CMake toolchain file for GCCSDK GCC 10 (arm-riscos-gnueabihf), for
# libraries built with CMake (OpenAL Soft). Uses the environment set by
# build/env.sh: GCCSDK_ENV (the compiler) and STAGE (our libraries).
#
#   source build/env.sh
#   cmake SRC -DCMAKE_TOOLCHAIN_FILE=.../build/riscos.cmake ...
#
# Compiler flags are not set here: pass -DCMAKE_C_FLAGS="$RO_CFLAGS ..." so
# every library gets the same flags (-fstack-clash-protection included).
set(CMAKE_SYSTEM_NAME GNU)          # as GCCSDK's own toolchain file: UNIX is true
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   $ENV{GCCSDK_ENV}/bin/arm-riscos-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER $ENV{GCCSDK_ENV}/bin/arm-riscos-gnueabihf-g++)
set(CMAKE_AR           $ENV{GCCSDK_ENV}/bin/arm-riscos-gnueabihf-ar CACHE FILEPATH "")
set(CMAKE_RANLIB       $ENV{GCCSDK_ENV}/bin/arm-riscos-gnueabihf-ranlib CACHE FILEPATH "")
# Find headers and libraries in our stage and the toolchain only, never on
# the build machine; programs (build tools) on the build machine only.
set(CMAKE_FIND_ROOT_PATH $ENV{STAGE} $ENV{GCCSDK_ENV})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(BUILD_SHARED_LIBS OFF)
