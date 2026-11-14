#include "MemoryManager.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace paging {

double SimulationResult::hitRate() const noexcept {
    return requests == 0 ? 0.0 : static_cast<double>(hits) * 100.0 / requests;
}

double SimulationResult::faultRate() const noexcept {
    return requests == 0 ? 0.0 : static_cast<double>(faults) * 100.0 / requests;
}

MemoryManager::MemoryManager(std::size_t frameCapacity,
                             std::size_t swapCapacity,
                             std::filesystem::path swapFile)
    : frames_(frameCapacity), swap_(swapCapacity, std::move(swapFile)) {
    if (frameCapacity == 0) {
        throw std::invalid_argument("RAM frame count must be greater than zero.");
    }
}

SimulationResult MemoryManager::run(const std::vector<int>& references, Algorithm algorithm) {
    for (const int page : references) {
        if (page < 0) throw std::invalid_argument("Page numbers cannot be negative.");
    }
    std::fill(frames_.begin(), frames_.end(), std::nullopt);
    pageTable_.clear();
    for (const int page : references) pageTable_.try_emplace(page);
    swap_.clear();

    SimulationResult result;
    result.algorithm = algorithm;
    result.frameCapacity = frames_.size();
    result.swapCapacity = swap_.slots().size();

    for (std::size_t position = 0; position < references.size(); ++position) {
        const int page = references[position];
        ++result.requests;

        auto resident = std::find_if(frames_.begin(), frames_.end(),
                                     [page](const auto& frame) {
                                         return frame && frame->page == page;
                                     });
        if (resident != frames_.end()) {
            const auto frameIndex = static_cast<std::size_t>(std::distance(frames_.begin(), resident));
            (*resident)->lastUsed = position;
            ++result.hits;
            result.steps.push_back(snapshot(position + 1, page, "HIT",
                                             "Page " + std::to_string(page) + " is already in RAM frame " +
                                                 std::to_string(frameIndex + 1) + ".",
                                             std::nullopt, false, frameIndex));
            continue;
        }

        ++result.faults;
        const auto freeFrame = std::find_if(frames_.begin(), frames_.end(),
                                            [](const auto& frame) { return !frame; });
        std::size_t targetFrame = 0;
        std::optional<int> evictedPage;
        std::optional<std::size_t> releasedSwapSlot;
        const bool pageWasSwapped = swap_.contains(page);
        std::string action;

        if (freeFrame != frames_.end()) {
            targetFrame = static_cast<std::size_t>(std::distance(frames_.begin(), freeFrame));
            if (pageWasSwapped) {
                swap_.take(page);
                pageTable_[page].inSwap = false;
                ++result.swapIns;
                action = "Page " + std::to_string(page) + " faulted and was restored from swap into free frame " +
                         std::to_string(targetFrame + 1) + ".";
            } else {
                action = "Page " + std::to_string(page) + " faulted and was loaded from backing storage into free frame " +
                         std::to_string(targetFrame + 1) + ".";
            }
        } else {
            std::vector<FrameRecord> occupiedFrames;
            occupiedFrames.reserve(frames_.size());
            for (const auto& frame : frames_) occupiedFrames.push_back(*frame);
            targetFrame = selectVictim(occupiedFrames, references, position, algorithm);

            if (!swap_.hasFreeSlot() && !pageWasSwapped) {
                result.completed = false;
                result.error = "Out of swap space: cannot evict a page to load page " + std::to_string(page) + ".";
                result.steps.push_back(snapshot(position + 1, page, "FAULT",
                                                 "Page fault could not be serviced: " + result.error,
                                                 std::nullopt, false, std::nullopt));
                break;
            }

            if (pageWasSwapped) {
                releasedSwapSlot = swap_.take(page);
                pageTable_[page].inSwap = false;
                ++result.swapIns;
            }
            const int victimPage = frames_[targetFrame]->page;
            const std::size_t swapSlot = swap_.put(victimPage, releasedSwapSlot);
            auto& victimEntry = pageTable_[victimPage];
            victimEntry.present = false;
            victimEntry.frame.reset();
            victimEntry.inSwap = true;
            evictedPage = victimPage;
            ++result.swapOuts;
            action = "Evicted page " + std::to_string(victimPage) + " from frame " +
                     std::to_string(targetFrame + 1) + " to swap slot " + std::to_string(swapSlot + 1) + ". ";
            if (pageWasSwapped) {
                action += "Restored page " + std::to_string(page) + " from swap into that frame.";
            } else {
                action += "Loaded page " + std::to_string(page) + " from backing storage into that frame.";
            }
        }

        frames_[targetFrame] = FrameRecord{page, position, position};
        auto& currentEntry = pageTable_[page];
        currentEntry.present = true;
        currentEntry.frame = targetFrame;
        currentEntry.inSwap = false;
        result.steps.push_back(snapshot(position + 1, page, "FAULT", action,
                                         evictedPage, pageWasSwapped, targetFrame));
    }

    return result;
}

StepResult MemoryManager::snapshot(std::size_t step, int page, const std::string& result,
                                   const std::string& action, std::optional<int> evictedPage,
                                   bool swappedIn, std::optional<std::size_t> frame) const {
    StepResult current;
    current.step = step;
    current.page = page;
    current.result = result;
    current.action = action;
    current.evictedPage = evictedPage;
    current.swappedIn = swappedIn;
    current.frame = frame;
    current.frames.reserve(frames_.size());
    for (const auto& item : frames_) {
        current.frames.push_back(item ? std::optional<int>(item->page) : std::nullopt);
    }
    current.swapSlots = swap_.slots();
    for (const auto& [knownPage, entry] : pageTable_) {
        current.pageTable.push_back(PageTableRow{knownPage, entry});
    }
    return current;
}

} // namespace paging
