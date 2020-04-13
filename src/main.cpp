#include <iostream>
#include <memory>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "renderer.h"


int main(int argc, char* argv[]) {
    std::cout << "GLFW: " << glfwGetVersionString() << std::endl;

    // initialize GLFW
    if (glfwInit() != GLFW_TRUE) {
        return 1;
    }

    // create window
    glfwInitHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwInitHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto win = std::shared_ptr<GLFWwindow>(
        glfwCreateWindow(800, 600, "Vulkan Test", nullptr, nullptr),
        [](auto win) {
            glfwDestroyWindow(win);
        }
    );

    // main loop
    while (!glfwWindowShouldClose(win.get())) {
        glfwPollEvents();
    }

    // terminate GLFW
    glfwTerminate();

    return 0;
}
