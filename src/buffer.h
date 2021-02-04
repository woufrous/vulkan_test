#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "device.h"
#include "utils.h"

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

struct BufferDesc {
    VkDeviceSize size;
    VkBufferUsageFlags buf_usage_flags;
    VkMemoryPropertyFlags mem_prop_flags;
};

inline void create_buffer(const VulkanDevice dev, const BufferDesc& desc, VkBuffer* buf, VkDeviceMemory* mem) {
    auto buf_info = VkBufferCreateInfo{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = desc.size;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buf_info.usage = desc.buf_usage_flags;

    {
        auto res = vkCreateBuffer(dev.logical, &buf_info, nullptr, buf);
        if (res != VK_SUCCESS) {
            throw VulkanError("Error creating vertex Buffer", res);
        }
    }

    auto mem_reqs = VkMemoryRequirements{};
    vkGetBufferMemoryRequirements(dev.logical, *buf, &mem_reqs);
    auto mem_props = VkPhysicalDeviceMemoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(dev.physical, &mem_props);

    auto malloc_info = VkMemoryAllocateInfo{};
    malloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    malloc_info.allocationSize = mem_reqs.size;
    // TODO: actually find matching memory type
    malloc_info.memoryTypeIndex = 0;

    {
        auto res = vkAllocateMemory(dev.logical, &malloc_info, nullptr, mem);
        if (res != VK_SUCCESS) {
            throw VulkanError("Error allocating Memory", res);
        }
    }

    vkBindBufferMemory(dev.logical, *buf, *mem, 0);
}
