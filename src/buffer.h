#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "command.h"
#include "device.h"
#include "utils.h"

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
    glm::vec2 uv;

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
    std::array<VkVertexInputAttributeDescription, 3>
    get_attrib_desc() {
        std::array<VkVertexInputAttributeDescription, 3> ret;
        ret[0].binding = 0;
        ret[0].location = 0;
        ret[0].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
        ret[0].offset = offsetof(Vertex, pos);
        ret[1].binding = 0;
        ret[1].location = 1;
        ret[1].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
        ret[1].offset = offsetof(Vertex, color);
        ret[2].binding = 0;
        ret[2].location = 2;
        ret[2].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
        ret[2].offset = offsetof(Vertex, uv);
        return ret;
    }
};

inline const std::vector<Vertex> vertices = {
    Vertex{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    Vertex{{0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    Vertex{{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    Vertex{{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
};

inline const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

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
    malloc_info.memoryTypeIndex = find_memory_type(
        dev.physical, mem_reqs.memoryTypeBits,
        desc.mem_prop_flags
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
    auto mem_tx_buf = OneTimeCommandBuffer(dev.logical, cmd_pool);
    auto cmd_executor = RAIICommandBufferExecutor(mem_tx_buf, tx_queue);

    auto region = VkBufferCopy{};
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = size;
    vkCmdCopyBuffer(mem_tx_buf, src_buf, dst_buf, 1, &region);
}

inline void copy_buffer_to_image(VkCommandBuffer cmd_buf, VkBuffer src_buf, VkImage dst_img, VkExtent3D extent) {
    auto copy_region = VkBufferImageCopy{};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;
    copy_region.imageOffset = VkOffset3D{};
    copy_region.imageExtent = extent;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.mipLevel = 0;
    vkCmdCopyBufferToImage(cmd_buf, src_buf, dst_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
}
