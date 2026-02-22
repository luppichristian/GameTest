cmake_minimum_required(VERSION 3.19)
project(GameTest C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(GMT_BUILD_SHARED   "Build as a shared library" OFF)
option(GMT_BUILD_EXAMPLE  "Build example"             ON)
option(GMT_BUILD_TOOLS    "Build auxiliary tools"     ON)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
set(GMT_SOURCES
    src/Assert.c
    src/GameTest.c
    src/InputState.c
    src/Log.c
    src/Memory.c
    src/Pin.c
    src/Record.c
    src/Signal.c
    src/Track.c
    src/Util.c
)

if(WIN32)
    list(APPEND GMT_SOURCES src/PlatformWin32.c)
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

if(GMT_BUILD_EXAMPLE)
    include(FetchContent)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    FetchContent_MakeAvailable(glfw)

    add_executable(GameTest-Game "example/Game.c")
    target_link_libraries(GameTest-Game PRIVATE GameTest glfw opengl32)
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

if(GMT_BUILD_TOOLS AND WIN32)
    add_executable(GameTest-Launch-Isolated tools/LaunchIsolatedWin32.c)
endif()