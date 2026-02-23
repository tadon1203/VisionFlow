cmake_minimum_required(VERSION 4.0.0)
project(VisionFlow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(kCompileCommandsMirrorDir "${CMAKE_SOURCE_DIR}/build")
file(MAKE_DIRECTORY "${kCompileCommandsMirrorDir}")

include(FetchContent)
include(CTest)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

if (BUILD_TESTING)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

file(GLOB_RECURSE SOURCES 
    CONFIGURE_DEPENDS
    "src/*.cpp"
    "include/VisionFlow/*.hpp"
)

add_executable(${PROJECT_NAME} ${SOURCES})

add_custom_target(syncCompileCommands ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${kCompileCommandsMirrorDir}/compile_commands.json"
    COMMENT "Sync compile_commands.json to ${kCompileCommandsMirrorDir}"
    VERBATIM)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    src
    third_party
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    spdlog::spdlog
)

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11
        dxgi
        windowsapp
    )
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
