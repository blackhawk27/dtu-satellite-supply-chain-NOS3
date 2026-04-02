cmake_minimum_required(VERSION 3.5)
project(CFS_NOISY_APP C)

set(APP_SRC_FILES
    fsw/src/noisy_app.c
)

add_cfe_app(noisy_app ${APP_SRC_FILES})

# Needed for GENERIC_EPS_Hk_tlm_t + GENERIC_EPS_HK_TLM_MID include chain.
target_include_directories(noisy_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_eps/fsw/cfs/platform_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_eps/fsw/cfs/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_eps/fsw/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_eps/fsw/standalone
    ${CMAKE_CURRENT_SOURCE_DIR}/../hwlib/fsw/public_inc
)
