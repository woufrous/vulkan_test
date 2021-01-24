cmake_minimum_required(VERSION 3.0.0)
project(vulkan_course VERSION 0.1.0)

find_package(glfw3 3.3 REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(vulkan_course
    src/main.cpp
)
target_link_libraries(vulkan_course
    PUBLIC
        glfw
        Vulkan::Vulkan
)
target_compile_features(vulkan_course
    PUBLIC
        cxx_std_17
)
target_compile_options(vulkan_course
    PUBLIC
        -Wall -Wextra -Wpedantic
)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
