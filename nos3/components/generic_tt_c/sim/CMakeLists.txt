project(generic_tt_c_sim)

find_package(ITC_Common REQUIRED QUIET COMPONENTS itc_logger)
find_package(NOSENGINE REQUIRED QUIET COMPONENTS common transport client)

include_directories(inc
                    ${sim_common_SOURCE_DIR}/inc
                    ${ITC_Common_INCLUDE_DIRS}
                    ${NOSENGINE_INCLUDE_DIRS})

set(generic_tt_c_sim_src
    src/generic_tt_c_hardware_model.cpp
    src/generic_tt_c_42_data_provider.cpp
    src/generic_tt_c_data_provider.cpp
    src/generic_tt_c_data_point.cpp
)

file(GLOB generic_tt_c_sim_inc inc/*.hpp)

set(generic_tt_c_sim_libs
    sim_common
    ${ITC_Common_LIBRARIES}
    ${NOSENGINE_LIBRARIES}
)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH}:$ORIGIN/../lib")

add_library(generic_tt_c_sim SHARED ${generic_tt_c_sim_src} ${generic_tt_c_sim_inc})
target_link_libraries(generic_tt_c_sim ${generic_tt_c_sim_libs})
install(TARGETS generic_tt_c_sim LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
