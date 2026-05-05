cmake_minimum_required(VERSION 2.6.4)
project(CFS_GENERIC_GNSS_APP C)

include(../../../ComponentSettings.cmake)

include_directories(public_inc)
include_directories(mission_inc)
include_directories(platform_inc)
include_directories(src)

include_directories(${hwlib_MISSION_DIR}/fsw/public_inc)
# Pulls in MGR's headers so we can include mgr_msg.h, mgr_msgids.h, and
# mgr_app.h to subscribe to MGR HK and gate DATA_TLM on SCIENCE. Mirrors
# the path the sample app effectively resolves through the mission build's
# APPLICATION_PLATFORM_INC_LIST.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../mgr/fsw/cfs/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../mgr/fsw/cfs/platform_inc)

aux_source_directory(src APP_SRC_FILES)

add_cfe_app(generic_gnss ${APP_SRC_FILES})
