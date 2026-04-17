cmake_minimum_required(VERSION 3.10)
project(reader)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-deprecated-declarations")
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
else ()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else ()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_DEBUG")
    endif ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
endif ()

add_executable(reader 
    src/main.cpp src/state.cpp src/image_loader.cpp

    external/zip/src/zip.c

    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/misc/cpp/imgui_stdlib.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)

set(GLEW_DIR external/glew-2.3.1-win32)
set(GLFW_DIR external/glfw-3.4.bin.WIN64)

target_include_directories(reader PRIVATE 
    external 
    external/imgui 
    external/glaze/include
    external/rapidfuzz-cpp
    external/zip/src
    ${GLEW_DIR}/include
    ${GLFW_DIR}/include
)
target_link_directories(reader PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
)
target_link_libraries(reader PRIVATE glfw3 opengl32 glew32)
set_target_properties(reader PROPERTIES LINKER_LANGUAGE CXX)

add_custom_command(
    TARGET reader PRE_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${GLEW_DIR}/bin/Release/x64/glew32.dll" 
            "${CMAKE_CURRENT_SOURCE_DIR}/${GLFW_DIR}/lib-vc2022/glfw3.dll" 
            "$<TARGET_FILE_DIR:reader>"
    COMMENT "Copying glew32.dll, glfw3.dll to bin dir"
)
add_custom_command(
    TARGET reader PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/lib"
)
add_custom_command(
    TARGET reader PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${GLEW_DIR}/lib/Release/x64/glew32s.lib" 
            "${CMAKE_CURRENT_SOURCE_DIR}/${GLEW_DIR}/lib/Release/x64/glew32.lib" 
            "${CMAKE_CURRENT_SOURCE_DIR}/${GLFW_DIR}/lib-vc2022/glfw3.lib" 
            "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    COMMENT "Copying glew32{,s}.lib, glfw3.lib to lib dir"
)
