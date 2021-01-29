#pragma once

#include <cstdint>
#include <istream>
#include <fstream>
#include <filesystem>
#include <optional>
#include <vector>

inline std::vector<uint8_t> load_file(std::istream& ins) {
    ins.seekg(0, std::ios::end);
    auto fsize = ins.tellg();
    auto ret = std::vector<uint8_t>(fsize);
    ins.seekg(0);

    ins.read(reinterpret_cast<char*>(ret.data()), fsize);
    return ret;
}

inline std::vector<uint8_t> load_file(const std::filesystem::path& fpath) {
    if (!std::filesystem::exists(fpath)) {
        throw std::runtime_error("file not found");
    }
    auto ifs = std::ifstream(fpath);
    return load_file(ifs);
}

template <typename P, typename C>
std::optional<uint32_t> filter_queues(const C& cntnr, P predicate) {
    uint32_t ret = 0;
    for (const auto& it : cntnr) {
        if (predicate(it)) return ret;
        ++ret;
    }
    return std::nullopt;
}

class VulkanError : public std::runtime_error {
    public:
        VulkanError(const char* what, VkResult ec) :
        std::runtime_error(what), ec_(ec) {}

        VkResult get_error() const noexcept {
            return ec_;
        }
    private:
        VkResult ec_;
};
