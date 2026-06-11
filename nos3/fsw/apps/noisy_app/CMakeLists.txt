cmake_minimum_required(VERSION 3.5)
project(CFS_NOISY_APP C)

set(APP_SRC_FILES
    fsw/src/noisy_app.c
)

add_cfe_app(noisy_app ${APP_SRC_FILES})

# This app's own MIDs (noisy_app_msgids.h).
target_include_directories(noisy_app PUBLIC
    fsw/platform_inc
)

# For GENERIC_EPS_HK_TLM_MID (the EPS_SPOOF/OVERRIDE scenario forges this telemetry MID).
target_include_directories(noisy_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_eps/fsw/cfs/platform_inc
)

# OPCODE_GNSS_SPOOF/DRIFT: the victim GNSS app's real headers (for exact struct offsets).
# generic_gnss_app.h pulls in hwlib.h plus the GNSS msg/msgid/platform headers, so add
# the same include roots generic_gnss's own CMakeLists uses.
target_include_directories(noisy_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/platform_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/mission_inc
    ${hwlib_MISSION_DIR}/fsw/public_inc
)

# dlopen/dlsym for runtime symbol resolution. ${CMAKE_DL_LIBS} is "dl" on older glibc
# and empty on glibc >= 2.34 (where dl* live in libc) - portable either way.
target_link_libraries(noisy_app ${CMAKE_DL_LIBS})

# RTLD_NOLOAD is a GNU extension - it needs _GNU_SOURCE before <dlfcn.h>. Define it on
# the command line so include order in the source does not matter.
target_compile_definitions(noisy_app PRIVATE _GNU_SOURCE)
