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

static inline uint32_t find_memory_type(VkPhysicalDevice dev, uint32_t type_filter, const VkMemoryPropertyFlags& props) {
    auto mem_props = VkPhysicalDeviceMemoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(dev, &mem_props);
    for (uint32_t i=0; i<mem_props.memoryTypeCount; ++i) {
        if (
            (type_filter & (1<<i)) &&
            ((mem_props.memoryTypes[i].propertyFlags & props) == props)
        ) {
            return i;
        }
    }
    return 0;
}

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
    malloc_info.memoryTypeIndex = find_memory_type(
        dev.physical, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    {
        auto res = vkAllocateMemory(dev.logical, &malloc_info, nullptr, mem);
        if (res != VK_SUCCESS) {
            throw VulkanError("Error allocating Memory", res);
        }
    }

    vkBindBufferMemory(dev.logical, *buf, *mem, 0);
}

inline void copy_buffer(const VulkanDevice dev, VkQueue tx_queue, VkCommandPool cmd_pool, VkBuffer src_buf, VkBuffer dst_buf, VkDeviceSize size) {
    auto cmd_buf_info = VkCommandBufferAllocateInfo{};
    cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buf_info.commandPool = cmd_pool;
    cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buf_info.commandBufferCount = 1;

    auto mem_tx_buf = VkCommandBuffer{};
    vkAllocateCommandBuffers(dev.logical, &cmd_buf_info, &mem_tx_buf);

    auto begin_info = VkCommandBufferBeginInfo{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(mem_tx_buf, &begin_info);

    auto region = VkBufferCopy{};
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = size;
    vkCmdCopyBuffer(mem_tx_buf, src_buf, dst_buf, 1, &region);

    vkEndCommandBuffer(mem_tx_buf);

    auto submit_info = VkSubmitInfo{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &mem_tx_buf;
    vkQueueSubmit(tx_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(tx_queue);

    vkFreeCommandBuffers(dev.logical, cmd_pool, 1, &mem_tx_buf);
}
