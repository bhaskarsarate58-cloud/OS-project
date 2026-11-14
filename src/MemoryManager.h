#pragma once

#include "PageReplacement.h"
#include "SwapManager.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace paging {

struct PageTableEntry {
    bool present = false;
    std::optional<std::size_t> frame;
    bool inSwap = false;
};

struct PageTableRow {
    int page;
    PageTableEntry entry;
};

struct StepResult {
    std::size_t step = 0;
    int page = 0;
    std::string result;
    std::string action;
    std::optional<int> evictedPage;
    bool swappedIn = false;
    std::optional<std::size_t> frame;
    std::vector<std::optional<int>> frames;
    std::vector<std::optional<int>> swapSlots;
    std::vector<PageTableRow> pageTable;
};

struct SimulationResult {
    Algorithm algorithm = Algorithm::FIFO;
    std::size_t frameCapacity = 0;
    std::size_t swapCapacity = 0;
    bool completed = true;
    std::string error;
    std::size_t requests = 0;
    std::size_t hits = 0;
    std::size_t faults = 0;
    std::size_t swapIns = 0;
    std::size_t swapOuts = 0;
    std::vector<StepResult> steps;

    double hitRate() const noexcept;
    double faultRate() const noexcept;
};

class MemoryManager {
public:
    MemoryManager(std::size_t frameCapacity,
                  std::size_t swapCapacity,
                  std::filesystem::path swapFile);

    SimulationResult run(const std::vector<int>& references, Algorithm algorithm);

private:
    StepResult snapshot(std::size_t step, int page, const std::string& result,
                        const std::string& action, std::optional<int> evictedPage,
                        bool swappedIn, std::optional<std::size_t> frame) const;

    std::vector<std::optional<FrameRecord>> frames_;
    SwapManager swap_;
    std::map<int, PageTableEntry> pageTable_;
};

} // namespace paging
