#include <iostream>
#include <string>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>


int main(int, char**) {
    std::cout << "GLFW: " << glfwGetVersionString() << std::endl;

    glfwInit();

    glfwInitHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto win = glfwCreateWindow(800, 600, "Vulkan Test", nullptr, nullptr);

    uint32_t ext_cnt = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_cnt, nullptr);

    std::cout << "Vulkan extension count: " << ext_cnt << std::endl;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(win);
    glfwTerminate();

    return 0;
}
