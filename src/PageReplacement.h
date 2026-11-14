#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace paging {

enum class Algorithm {
    FIFO,
    LRU,
    Optimal
};

struct FrameRecord {
    int page;
    std::size_t loadedAt;
    std::size_t lastUsed;
};

Algorithm parseAlgorithm(const std::string& value);
std::string algorithmName(Algorithm algorithm);
std::size_t selectVictim(const std::vector<FrameRecord>& frames,
                         const std::vector<int>& references,
                         std::size_t currentPosition,
                         Algorithm algorithm);

} // namespace paging
