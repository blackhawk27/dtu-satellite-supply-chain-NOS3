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

# For GENERIC_EPS_HK_TLM_MID (the EPS_SPOOF scenario forges this telemetry MID).
target_include_directories(noisy_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_eps/fsw/cfs/platform_inc
)
