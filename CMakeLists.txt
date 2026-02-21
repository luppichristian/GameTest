cmake_minimum_required(VERSION 3.19)
project(GameTest C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GMT_BUILD_SHARED   "Build as a shared library" OFF)
option(GMT_BUILD_EXAMPLES "Build examples"            ON)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
if(WIN32)
    file(GLOB GMT_SOURCES "src/*.c")
else()
    message(FATAL_ERROR "GameTest: platform not supported (only Win32 is implemented).")
endif()

if(GMT_BUILD_SHARED)
    add_library(GameTest SHARED ${GMT_SOURCES})
    target_compile_definitions(GameTest
        PRIVATE GMT_SHARED GMT_EXPORTS
        PUBLIC  GMT_SHARED
    )
else()
    add_library(GameTest STATIC ${GMT_SOURCES})
endif()

target_include_directories(GameTest PUBLIC include/)

if(WIN32)
    target_link_libraries(GameTest PRIVATE comdlg32 psapi)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(GMT_BUILD_EXAMPLES)
    file(GLOB EXAMPLE_SOURCES "examples/*.c")
    foreach(EXAMPLE_FILE ${EXAMPLE_SOURCES})
        get_filename_component(EXAMPLE_NAME "${EXAMPLE_FILE}" NAME_WE)
        add_executable(GameTest-${EXAMPLE_NAME} "${EXAMPLE_FILE}")
        target_link_libraries(GameTest-${EXAMPLE_NAME} PRIVATE GameTest)
    endforeach()
endif()
