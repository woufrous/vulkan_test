#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static
    VkVertexInputBindingDescription
    get_binding_desc() {
        auto ret = VkVertexInputBindingDescription{};
        ret.binding = 0;
        ret.stride = sizeof(Vertex);
        ret.inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;

        return ret;
    }

    static
    std::array<VkVertexInputAttributeDescription, 2>
    get_attrib_desc() {
        std::array<VkVertexInputAttributeDescription, 2> ret;
        ret[0].binding = 0;
        ret[0].location = 0;
        ret[0].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
        ret[0].offset = offsetof(Vertex, pos);
        ret[1].binding = 0;
        ret[1].location = 1;
        ret[1].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
        ret[1].offset = offsetof(Vertex, color);
        return ret;
    }
};

inline std::vector<Vertex> vertices = {
    Vertex{{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    Vertex{{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    Vertex{{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
};
