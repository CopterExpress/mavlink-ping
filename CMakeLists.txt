cmake_minimum_required(VERSION 3.15)
project(mavlink-ping C)

set(CMAKE_C_STANDARD 99)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)

set(SOURCE_MAVLINK_ID 250)
# MAV_COMP_ID_PERIPHERAL
set(SOURCE_MAVLINK_COMPONENT 158)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVERSION_MAJOR=${VERSION_MAJOR} -DVERSION_MINOR=${VERSION_MINOR}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSOURCE_MAVLINK_ID=${SOURCE_MAVLINK_ID}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSOURCE_MAVLINK_COMPONENT=${SOURCE_MAVLINK_COMPONENT}")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

if((NOT EXISTS "${PROJECT_SOURCE_DIR}/lib/mavlink/v2.0/protocol.h"))
    message(FATAL_ERROR "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again.")
endif()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address,undefined,leak")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address,undefined,leak")

add_executable(mavlink-ping main.c)
target_include_directories(mavlink-ping PUBLIC ${PROJECT_SOURCE_DIR}/lib/mavlink/v2.0)
target_link_libraries(mavlink-ping m)

install(TARGETS mavlink-ping DESTINATION bin)
