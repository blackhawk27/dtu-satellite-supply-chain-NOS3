cmake_minimum_required(VERSION 3.5)
project(CFS_BEACON_APP C)

set(APP_SRC_FILES
    fsw/src/beacon_app.c
)

add_cfe_app(beacon_app ${APP_SRC_FILES})

target_include_directories(beacon_app PUBLIC
    fsw/platform_inc
)
