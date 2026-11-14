#include "PageReplacement.h"

#include <algorithm>
#include <stdexcept>

namespace paging {

Algorithm parseAlgorithm(const std::string& value) {
    if (value == "FIFO") return Algorithm::FIFO;
    if (value == "LRU") return Algorithm::LRU;
    if (value == "OPT" || value == "Optimal" || value == "OPTIMAL") {
        return Algorithm::Optimal;
    }
    throw std::invalid_argument("Algorithm must be FIFO, LRU, or OPT.");
}

std::string algorithmName(Algorithm algorithm) {
    switch (algorithm) {
    case Algorithm::FIFO: return "FIFO";
    case Algorithm::LRU: return "LRU";
    case Algorithm::Optimal: return "Optimal";
    }
    throw std::logic_error("Unknown page replacement algorithm.");
}

std::size_t selectVictim(const std::vector<FrameRecord>& frames,
                         const std::vector<int>& references,
                         std::size_t currentPosition,
                         Algorithm algorithm) {
    if (frames.empty()) {
        throw std::invalid_argument("Cannot select a victim from empty memory.");
    }

    std::size_t victim = 0;
    if (algorithm == Algorithm::FIFO) {
        for (std::size_t i = 1; i < frames.size(); ++i) {
            if (frames[i].loadedAt < frames[victim].loadedAt) victim = i;
        }
        return victim;
    }
    if (algorithm == Algorithm::LRU) {
        for (std::size_t i = 1; i < frames.size(); ++i) {
            if (frames[i].lastUsed < frames[victim].lastUsed) victim = i;
        }
        return victim;
    }

    std::size_t farthestNextUse = 0;
    bool victimIsNeverUsedAgain = false;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto next = std::find(references.begin() + static_cast<std::ptrdiff_t>(currentPosition + 1),
                                    references.end(), frames[i].page);
        if (next == references.end()) {
            victim = i;
            victimIsNeverUsedAgain = true;
            continue;
        }
        if (!victimIsNeverUsedAgain) {
            const auto distance = static_cast<std::size_t>(std::distance(references.begin(), next));
            if (distance > farthestNextUse) {
                farthestNextUse = distance;
                victim = i;
            }
        }
    }
    return victim;
}

} // namespace paging
