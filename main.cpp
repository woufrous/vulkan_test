#include <iostream>
#include <string>

#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <vulkan/vulkan.h>


int main(int, char**) {
    std::cout << "GLFW: " << glfwGetVersionString() << std::endl;
    return 0;
}
