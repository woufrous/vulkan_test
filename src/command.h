#pragma once

#include <vulkan/vulkan.h>

#include "utils.h"

class OneTimeCommandBuffer {
    public:
        OneTimeCommandBuffer(VkDevice dev, VkCommandPool cmd_pool, VkQueue queue) :
            dev_{dev},
            cmd_pool_{cmd_pool},
            queue_{queue},
            cmd_buf_{VK_NULL_HANDLE},
            is_open_{false}
        {
            auto cmd_buf_info = VkCommandBufferAllocateInfo{};
            cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmd_buf_info.commandPool = cmd_pool;
            cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmd_buf_info.commandBufferCount = 1;

            if (auto res = vkAllocateCommandBuffers(dev_, &cmd_buf_info, &cmd_buf_); res != VK_SUCCESS) {
                throw VulkanError("Error allocating CommandBuffer", res);
            }

            begin();
        }

        ~OneTimeCommandBuffer() {
            submit();
            vkFreeCommandBuffers(dev_, cmd_pool_, 1, &cmd_buf_);
        }

        operator VkCommandBuffer() const {
            return cmd_buf_;
        }

    private:
        void begin() const {
            auto begin_info = VkCommandBufferBeginInfo{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (auto res = vkBeginCommandBuffer(cmd_buf_, &begin_info); res != VK_SUCCESS) {
                throw VulkanError("Error beginning CommandBuffer", res);
            }
            is_open_ = true;
        }

        void end() const {
            if (is_open_) {
                if (auto res = vkEndCommandBuffer(cmd_buf_); res != VK_SUCCESS) {
                    throw VulkanError("Error ending CommandBuffer", res);
                }
            }
        }

        void submit() const {
            end();

            auto submit_info = VkSubmitInfo{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd_buf_;
            if (auto res = vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE); res != VK_SUCCESS) {
                throw VulkanError("Error submitting Queue", res);
            }
            if (auto res = vkQueueWaitIdle(queue_); res != VK_SUCCESS) {
                throw VulkanError("Error waiting for queue idle ", res);
            }
        }

        VkDevice dev_;
        VkCommandPool cmd_pool_;
        VkQueue queue_;
        VkCommandBuffer cmd_buf_;

        mutable bool is_open_;
};
