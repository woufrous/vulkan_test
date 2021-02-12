cmake_minimum_required(VERSION 3.0.0)
project(vulkan_course VERSION 0.1.0)

include(CTest)
enable_testing()

#find_package(glfw3 3.3 REQUIRED)
find_package(Vulkan REQUIRED)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_custom_command(
    OUTPUT vert.spv
    COMMAND glslc -o $<TARGET_FILE_DIR:vulkan_course>/vert.spv -fshader-stage=vertex ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/test.vert.glsl
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/test.vert.glsl
)

add_custom_command(
    OUTPUT frag.spv
    COMMAND glslc -o $<TARGET_FILE_DIR:vulkan_course>/frag.spv -fshader-stage=fragment ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/test.frag.glsl
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/test.frag.glsl
)

add_executable(vulkan_course
    src/main.cpp
    vert.spv
    frag.spv
)
target_link_libraries(vulkan_course
    PUBLIC
        Vulkan::Vulkan
        ${CONAN_LIBS}
)
target_compile_features(vulkan_course
    PUBLIC
        cxx_std_20
)
target_compile_options(vulkan_course
    PUBLIC
        -Wall -Wextra -Wpedantic
)
set_target_properties(vulkan_course
    PROPERTIES
        CXX_EXTENSIONS NO
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
