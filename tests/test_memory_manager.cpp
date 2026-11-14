#include "../src/MemoryManager.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const std::vector<int> kReferences{1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
const std::filesystem::path kSwapFile =
    std::filesystem::temp_directory_path() / "demand_paging_test_swap.txt";

void checkSnapshotConsistency(const paging::StepResult& step) {
    std::set<int> resident;
    std::set<int> swapped;
    for (const auto& page : step.frames) {
        if (page) assert(resident.insert(*page).second);
    }
    for (const auto& page : step.swapSlots) {
        if (page) assert(swapped.insert(*page).second);
    }
    for (const int page : resident) assert(swapped.count(page) == 0);

    for (const auto& row : step.pageTable) {
        assert(row.entry.present == row.entry.frame.has_value());
        assert(row.entry.inSwap == (swapped.count(row.page) != 0));
        if (row.entry.present) {
            assert(*step.frames[*row.entry.frame] == row.page);
        }
    }
}

paging::SimulationResult simulate(paging::Algorithm algorithm,
                                  const std::vector<int>& references = kReferences,
                                  std::size_t frames = 3,
                                  std::size_t swapSlots = 8) {
    paging::MemoryManager manager(frames, swapSlots, kSwapFile);
    return manager.run(references, algorithm);
}

void testAssignmentReferenceString() {
    const auto fifo = simulate(paging::Algorithm::FIFO);
    const auto lru = simulate(paging::Algorithm::LRU);
    const auto optimal = simulate(paging::Algorithm::Optimal);

    assert(fifo.completed && fifo.requests == 12 && fifo.hits == 3 && fifo.faults == 9);
    assert(lru.completed && lru.requests == 12 && lru.hits == 2 && lru.faults == 10);
    assert(optimal.completed && optimal.requests == 12 && optimal.hits == 5 && optimal.faults == 7);
    assert(fifo.steps[9].evictedPage == 1);
    assert(lru.steps[9].evictedPage == 5);
    assert(optimal.steps[3].evictedPage == 3);
    assert(fifo.swapIns + fifo.swapOuts > 0);
    assert(fifo.hitRate() + fifo.faultRate() > 99.99);
    for (const auto* result : {&fifo, &lru, &optimal}) {
        assert(result->steps.size() == kReferences.size());
        for (const auto& step : result->steps) checkSnapshotConsistency(step);
    }
}

void testRepeatedPagesAndEmptySequence() {
    const auto repeated = simulate(paging::Algorithm::LRU, {4, 4, 4}, 1, 2);
    assert(repeated.completed && repeated.requests == 3);
    assert(repeated.faults == 1 && repeated.hits == 2);
    assert(repeated.swapIns == 0 && repeated.swapOuts == 0);

    const auto twoFrames = simulate(paging::Algorithm::FIFO, {1, 2, 1, 3}, 2, 2);
    assert(twoFrames.completed && twoFrames.faults == 3 && twoFrames.hits == 1);
    assert(twoFrames.steps[3].evictedPage == 1);

    const auto empty = simulate(paging::Algorithm::FIFO, {}, 2, 2);
    assert(empty.completed && empty.requests == 0 && empty.steps.empty());
    assert(empty.hitRate() == 0.0 && empty.faultRate() == 0.0);
}

void testSwapInReusesFullSwapSlot() {
    const auto result = simulate(paging::Algorithm::FIFO, {1, 2, 3, 1}, 2, 1);
    assert(result.completed && result.swapIns == 1 && result.swapOuts == 2);
    assert(result.steps[2].evictedPage == 1);
    assert(result.steps[3].swappedIn);
    assert(result.steps[3].evictedPage == 2);
    for (const auto& step : result.steps) checkSnapshotConsistency(step);
}

void testOutOfSwapIsReportedWithoutCorruptingState() {
    const auto result = simulate(paging::Algorithm::FIFO, {1, 2, 3}, 1, 1);
    assert(!result.completed);
    assert(result.requests == 3 && result.faults == 3 && result.hits == 0);
    assert(result.steps.size() == 3);
    assert(result.steps.back().action.find("Out of swap space") != std::string::npos);
    assert(result.steps.back().frames[0] == 2);
    assert(result.steps.back().swapSlots[0] == 1);
    checkSnapshotConsistency(result.steps.back());
}

void testInvalidConfigurationAndPages() {
    bool zeroFramesRejected = false;
    try {
        paging::MemoryManager invalid(0, 1, kSwapFile);
    } catch (const std::invalid_argument&) {
        zeroFramesRejected = true;
    }
    assert(zeroFramesRejected);

    bool zeroSwapRejected = false;
    try {
        paging::MemoryManager invalid(1, 0, kSwapFile);
    } catch (const std::invalid_argument&) {
        zeroSwapRejected = true;
    }
    assert(zeroSwapRejected);

    bool negativePageRejected = false;
    try {
        simulate(paging::Algorithm::FIFO, {-1}, 1, 1);
    } catch (const std::invalid_argument&) {
        negativePageRejected = true;
    }
    assert(negativePageRejected);
}

} // namespace

int main() {
    try {
        testAssignmentReferenceString();
        testRepeatedPagesAndEmptySequence();
        testSwapInReusesFullSwapSlot();
        testOutOfSwapIsReportedWithoutCorruptingState();
        testInvalidConfigurationAndPages();
        std::error_code error;
        std::filesystem::remove(kSwapFile, error);
        if (error) throw std::runtime_error("Could not remove temporary test swap file.");
        std::cout << "All demand-paging engine tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
