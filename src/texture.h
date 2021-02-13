#pragma once

#include <cstdint>
#include <memory>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <vulkan/vulkan.h>

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
