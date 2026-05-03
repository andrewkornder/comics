cmake_minimum_required(VERSION 3.10)
project(reader)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-deprecated-declarations -Wno-missing-braces")
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g")
    else ()
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        else ()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        endif ()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
    endif ()
endif ()



find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)

set(CLIP_ENABLE_IMAGE ON CACHE BOOL "Enable copying/pasting images")
set(CLIP_TESTS OFF CACHE BOOL "Disable tests for clip")
set(CLIP_EXAMPLES OFF CACHE BOOL "Disable examples for clip")
set(CLIP_INSTALL OFF CACHE BOOL "Disable install commands for clip")
add_subdirectory(external/clip)

add_subdirectory(external/glaze)

add_subdirectory(external/argparse)

add_subdirectory(external/zip)

set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(external/abseil-cpp)

set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(external/re2)



file(GLOB_RECURSE COMMON_SOURCE_FILES CONFIGURE_DEPENDS src/common/*.cpp)
file(GLOB_RECURSE GUI_SOURCE_FILES CONFIGURE_DEPENDS src/app/*.cpp)

file(GLOB_RECURSE INDEXER_SOURCE_FILES CONFIGURE_DEPENDS src/indexer/*.cpp)



add_executable(indexer ${COMMON_SOURCE_FILES} ${INDEXER_SOURCE_FILES})
target_include_directories(indexer PRIVATE
    ${VIPS_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)
target_link_libraries(indexer PRIVATE
    zip::zip
    glaze::glaze 
    re2::re2 
    argparse
    pcre2-8
    ${VIPS_LIBRARIES}
    ${GLIB_LIBRARIES}
)


add_executable(reader ${COMMON_SOURCE_FILES} ${GUI_SOURCE_FILES}
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/misc/cpp/imgui_stdlib.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(reader PRIVATE
    external 
    external/imgui
    external/rapidfuzz-cpp
    ${VIPS_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)
target_link_libraries(reader PRIVATE 
    zip::zip
    glaze::glaze
    re2::re2
    clip
    jq
    ${VIPS_LIBRARIES}
    ${GLIB_LIBRARIES}
)
if (WIN32) 
    target_link_libraries(reader PRIVATE opengl32 glew32 glfw3)
else()
    target_link_libraries(reader PRIVATE GLEW GL OpenGL glfw)
endif()
