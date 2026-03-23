cmake_minimum_required(VERSION 3.5)
project(CFS_NOISY_APP C)

set(APP_SRC_FILES
    fsw/src/noisy_app.c
)

add_cfe_app(noisy_app ${APP_SRC_FILES})