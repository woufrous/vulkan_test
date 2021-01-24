#pragma once

#include <cstdint>
#include <optional>

template <typename P, typename C>
std::optional<uint32_t> filter_queues(const C& cntnr, P predicate) {
    uint32_t ret = 0;
    for (const auto& it : cntnr) {
        if (predicate(it)) return ret;
        ++ret;
    }
    return std::nullopt;
}
