# dEQP target: riscos-mesa's EGL on the host harness's fake RISC OS.
# Set by build.sh: RISCOS_EGL_LIB (egl_riscos.c + fake_riscos.c, static),
# RISCOS_OSMESA_DIR (host libOSMesa.so), RISCOS_FAKE_INCLUDE.
message("*** Using riscos-mesa EGL on a fake RISC OS")
set(DEQP_TARGET_NAME "RISC OS (fake, host)")

set(DEQP_EGL_LIBRARIES ${RISCOS_EGL_LIB} -L${RISCOS_OSMESA_DIR} -lOSMesa -Wl,-rpath,${RISCOS_OSMESA_DIR})
set(DEQP_PLATFORM_LIBRARIES ${DEQP_EGL_LIBRARIES} pthread)
include_directories(${RISCOS_FAKE_INCLUDE})

# Below 2 GB (see tcuRiscosMain.cpp)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-pie")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-pie")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie -Wl,--wrap=pthread_create -Wl,--wrap=pthread_join -Wl,--wrap=main")

set(TCUTIL_PLATFORM_SRCS
	riscos/tcuRiscosPlatform.cpp
	riscos/tcuRiscosPlatform.hpp
	riscos/tcuRiscosMain.cpp
	)
