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

# For the OPCODE_GNSS_SPOOF/DRIFT direct-memory scenario: include the victim GNSS
# app's real headers so the GENERIC_GNSS_AppData_t layout (field offsets) is exact.
# The symbol itself is resolved at runtime via dlopen(RTLD_NOLOAD)+dlsym (the .so is
# loaded RTLD_LOCAL on this Linux target, so a link-time extern cannot reach it).
# generic_gnss_app.h pulls in hwlib.h plus the GNSS msg/msgid/platform headers, so
# we add the same include roots generic_gnss's own CMakeLists uses (src, platform_inc,
# mission_inc, and hwlib public_inc).
target_include_directories(noisy_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/platform_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/mission_inc
    ${hwlib_MISSION_DIR}/fsw/public_inc
)

# dlopen/dlsym for the runtime GNSS symbol resolution (NOISY_ResolveGnssAppData).
target_link_libraries(noisy_app dl)
