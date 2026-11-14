#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace paging {

class SwapManager {
public:
    SwapManager(std::size_t capacity, std::filesystem::path storagePath);

    void clear();
    bool contains(int page) const;
    bool hasFreeSlot() const;
    std::optional<std::size_t> take(int page);
    std::size_t put(int page, std::optional<std::size_t> preferredSlot = std::nullopt);
    const std::vector<std::optional<int>>& slots() const noexcept;

private:
    void persist() const;

    std::vector<std::optional<int>> slots_;
    std::filesystem::path storagePath_;
};

} // namespace paging
