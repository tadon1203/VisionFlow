cmake_minimum_required(VERSION 4.0.0)
project(VisionFlow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

file(GLOB_RECURSE SOURCES 
    CONFIGURE_DEPENDS
    "src/*.cpp"
    "include/VisionFlow/*.hpp"
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE include)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    spdlog::spdlog
)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        setupapi
    )
endif()
