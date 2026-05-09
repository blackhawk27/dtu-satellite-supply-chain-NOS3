project(generic_gnss_sim)

find_package(ITC_Common REQUIRED QUIET COMPONENTS itc_logger)
# Add the uart component so the FSW reads lat/lon over a NOS Engine UART
# bus (matches the libgps_sim wiring). Without `uart` here, the
# NosEngine::Uart::Uart symbol is undefined at link time.
find_package(NOSENGINE REQUIRED QUIET COMPONENTS common transport client uart)

include_directories(inc
                    ${sim_common_SOURCE_DIR}/inc
                    ${ITC_Common_INCLUDE_DIRS}
                    ${NOSENGINE_INCLUDE_DIRS})

set(generic_gnss_sim_src
    src/generic_gnss_hardware_model.cpp
    src/generic_gnss_42_data_provider.cpp
    src/generic_gnss_data_provider.cpp
    src/generic_gnss_data_point.cpp
)

file(GLOB generic_gnss_sim_inc inc/*.hpp)

set(generic_gnss_sim_libs
    sim_common
    ${ITC_Common_LIBRARIES}
    ${NOSENGINE_LIBRARIES}
)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH}:$ORIGIN/../lib")

add_library(generic_gnss_sim SHARED ${generic_gnss_sim_src} ${generic_gnss_sim_inc})
target_link_libraries(generic_gnss_sim ${generic_gnss_sim_libs})
install(TARGETS generic_gnss_sim LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
