cmake_minimum_required(VERSION 3.0.0)
project(vulkan_course VERSION 0.1.0)

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(vulkan_course main.cpp)
target_link_libraries(vulkan_course
    PUBLIC
        OpenGL::GL
        glfw
)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
