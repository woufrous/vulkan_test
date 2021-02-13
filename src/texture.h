#pragma once

#include <cstdint>
#include <memory>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <vulkan/vulkan.h>

#include "command.h"
#include "utils.h"

class Texture {
    public:
        Texture(const std::filesystem::path& fpath) : ptr_(nullptr, stbi_image_free) {
            int channels;
            ptr_ = std::unique_ptr<uint8_t, decltype(&stbi_image_free)>(
                reinterpret_cast<uint8_t*>(
                    stbi_load(fpath.c_str(), &width_, &height_, &channels, STBI_rgb_alpha)
                ),
                stbi_image_free
            );

            if (ptr_ == nullptr) {
                throw std::runtime_error("Error loading image");
            }
        }

        size_t size() const noexcept {
            return width_ * height_ * 4;
        }

        const uint8_t* data() const noexcept {
            return ptr_.get();
        }

        uint32_t width() const noexcept {
            return static_cast<uint32_t>(width_);
        }

        uint32_t height() const noexcept {
            return static_cast<uint32_t>(height_);
        }

        VkExtent3D extent() const noexcept {
            auto ret = VkExtent3D{};
            ret.width = width();
            ret.height = height();
            ret.depth = 1;
            return ret;
        }
    private:
        std::unique_ptr<uint8_t, decltype(&stbi_image_free)> ptr_;
        int width_;
        int height_;
};

struct ImageDesc {
    uint32_t width;
    uint32_t height;
    VkImageUsageFlags usage;
    VkMemoryPropertyFlags mem_props;

    VkExtent3D extent() const noexcept {
        auto ret = VkExtent3D{};
        ret.width = width;
        ret.height = height;
        ret.depth = 1;
        return ret;
    }
};

inline void create_image(VulkanDevice dev, const ImageDesc& desc, VkImage* img, VkDeviceMemory* mem) {
    auto img_info = VkImageCreateInfo{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.extent = desc.extent();
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage = desc.usage;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;

    {
        auto res = vkCreateImage(dev.logical, &img_info, nullptr, img);
        if (res != VK_SUCCESS) {
            throw VulkanError("Error creating image", res);
        }
    }

    auto mem_reqs = VkMemoryRequirements{};
    vkGetImageMemoryRequirements(dev.logical, *img, &mem_reqs);
    auto mem_props = VkPhysicalDeviceMemoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(dev.physical, &mem_props);

    auto malloc_info = VkMemoryAllocateInfo{};
    malloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    malloc_info.allocationSize = mem_reqs.size;
    malloc_info.memoryTypeIndex = find_memory_type(
        dev.physical, mem_reqs.memoryTypeBits,
        desc.mem_props
    );

    {
        auto res = vkAllocateMemory(dev.logical, &malloc_info, nullptr, mem);
        if (res != VK_SUCCESS) {
            throw VulkanError("Error allocating Memory", res);
        }
    }
    vkBindImageMemory(dev.logical, *img, *mem, 0);
}

inline void transition_image_layout(
    VkCommandBuffer cmd_buf, VkImage img,
    VkImageLayout old_layout, VkImageLayout new_layout
) {
    auto img_barrier = VkImageMemoryBarrier{};
    img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.oldLayout = old_layout;
    img_barrier.newLayout = new_layout;
    img_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.image = img;
    img_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    img_barrier.subresourceRange.layerCount = 1;
    img_barrier.subresourceRange.baseArrayLayer = 0;
    img_barrier.subresourceRange.levelCount = 1;
    img_barrier.subresourceRange.baseMipLevel = 0;

    VkPipelineStageFlags src_stage, dst_stage;
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        img_barrier.srcAccessMask = 0;

        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        img_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else {
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        img_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        img_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    vkCmdPipelineBarrier(cmd_buf, src_stage, dst_stage, 0,
        0, nullptr,     // memory barriers
        0, nullptr,     // buffer memory barriers
        1, &img_barrier // image memory barriers
    );
}


inline VkImageView create_image_view(VkDevice dev, VkImage img, VkFormat fmt) {
    auto ret = VkImageView{};

    auto iv_info = VkImageViewCreateInfo{};
    iv_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv_info.image = img;
    iv_info.format = fmt;
    iv_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv_info.subresourceRange.levelCount = 1;
    iv_info.subresourceRange.baseMipLevel = 0;
    iv_info.subresourceRange.layerCount = 1;
    iv_info.subresourceRange.baseArrayLayer = 0;

    if (auto res = vkCreateImageView(dev, &iv_info, nullptr, &ret); res != VK_SUCCESS) {
        throw VulkanError("Error creating ImageView", res);
    }

    return ret;
}

inline VkSampler create_texture_sampler(VulkanDevice dev) {
    auto ret = VkSampler{};

    auto dev_props = VkPhysicalDeviceProperties{};
    vkGetPhysicalDeviceProperties(dev.physical, &dev_props);

    auto sampler_info = VkSamplerCreateInfo{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.minFilter = VkFilter::VK_FILTER_LINEAR;
    sampler_info.magFilter = VkFilter::VK_FILTER_LINEAR;
    sampler_info.addressModeU = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = dev_props.limits.maxSamplerAnisotropy;
    sampler_info.borderColor = VkBorderColor::VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_TRUE;
    sampler_info.compareOp = VkCompareOp::VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;

    if (auto res = vkCreateSampler(dev.logical, &sampler_info, nullptr, &ret); res != VK_SUCCESS) {
        throw VulkanError("Error creating Sampler", res);
    }

    return ret;
}
