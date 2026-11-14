#include "SwapManager.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace paging {

SwapManager::SwapManager(std::size_t capacity, std::filesystem::path storagePath)
    : slots_(capacity), storagePath_(std::move(storagePath)) {
    if (capacity == 0) {
        throw std::invalid_argument("Swap capacity must be greater than zero.");
    }
}

void SwapManager::clear() {
    for (auto& slot : slots_) slot.reset();
    persist();
}

bool SwapManager::contains(int page) const {
    for (const auto& slot : slots_) {
        if (slot && *slot == page) return true;
    }
    return false;
}

bool SwapManager::hasFreeSlot() const {
    for (const auto& slot : slots_) {
        if (!slot) return true;
    }
    return false;
}

std::optional<std::size_t> SwapManager::take(int page) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i] && *slots_[i] == page) {
            slots_[i].reset();
            persist();
            return i;
        }
    }
    return std::nullopt;
}

std::size_t SwapManager::put(int page, std::optional<std::size_t> preferredSlot) {
    std::size_t destination = slots_.size();
    if (preferredSlot && *preferredSlot < slots_.size() && !slots_[*preferredSlot]) {
        destination = *preferredSlot;
    } else {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i]) {
                destination = i;
                break;
            }
        }
    }
    if (destination == slots_.size()) {
        throw std::runtime_error("Swap space is full.");
    }
    if (contains(page)) {
        throw std::logic_error("A page cannot occupy more than one swap slot.");
    }
    slots_[destination] = page;
    persist();
    return destination;
}

const std::vector<std::optional<int>>& SwapManager::slots() const noexcept {
    return slots_;
}

void SwapManager::persist() const {
    if (storagePath_.empty()) return;
    if (!storagePath_.parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(storagePath_.parent_path(), error);
        if (error) {
            throw std::runtime_error("Cannot create swap directory: " + error.message());
        }
    }
    std::ofstream output(storagePath_, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot open simulated swap file: " + storagePath_.string());
    }
    output << "# Simulated swap slots (slot page)\n";
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i]) output << i + 1 << ' ' << *slots_[i] << '\n';
    }
    if (!output) {
        throw std::runtime_error("Failed while writing simulated swap file.");
    }
}

} // namespace paging
