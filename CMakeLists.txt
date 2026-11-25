cmake_minimum_required(VERSION 3.16)
project(uskok LANGUAGES CXX)

add_executable(uskok src/main.cpp src/engine.cpp src/render.cpp src/platform.cpp)
target_compile_features(uskok PRIVATE cxx_std_20)
set_target_properties(uskok PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(uskok PRIVATE /W4 /permissive-)
else()
    target_compile_options(uskok PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_DL_LIBS)
    target_link_libraries(uskok PRIVATE ${CMAKE_DL_LIBS})
endif()
add_custom_command(TARGET uskok POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/usk.map" "$<TARGET_FILE_DIR:uskok>/usk.map")
