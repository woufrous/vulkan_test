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
    if (glfwVulkanSupported()) {
        std::cout << "Vulkan supported" << std::endl;
        uint32_t ext_cnt = 0;
        auto exts = glfwGetRequiredInstanceExtensions(&ext_cnt);
        std::cout << ext_cnt << " extensions required:\n";
        for (uint32_t i=0; i<ext_cnt; ++i) {
            std::cout << "\t" << exts[i] << "\n";
        }
    } else {
        std::cerr << "Vulkan is NOT supported!" << std::endl;
        return 1;
    }

    // create window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto win = glfwCreateWindow(800, 600, "Vulkan Test", nullptr, nullptr);
    glfwSetFramebufferSizeCallback(win, VulkanRenderer::win_resize_handler);
    if (win == nullptr) {
        std::cerr << "Error creating Window" << std::endl;
    }

    auto renderer = VulkanRenderer(win);
    try {
        renderer.init();
        glfwSetWindowUserPointer(win, reinterpret_cast<void*>(&renderer));

        while (!glfwWindowShouldClose(win)) {
            glfwPollEvents();
            renderer.draw_frame();
        }
        renderer.destroy();
    }
    catch (const VulkanError& ex) {
        std::cerr << ex.what() << std::endl;
        std::cerr << "Errorcode: " << ex.get_error() << std::endl;

        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }
    catch (...) {
        std::cerr << "Unhandled exception!" << std::endl;

        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }

    glfwDestroyWindow(win);
    // terminate GLFW
    glfwTerminate();

    return 0;
}
