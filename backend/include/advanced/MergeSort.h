#pragma once
// DIVIDE AND CONQUER: stable top-down merge sort.
//
//   divide   : split the range into two halves                     (O(1))
//   conquer  : sort each half recursively                          (2 * T(n/2))
//   combine  : merge the two sorted halves into one sorted range   (Theta(n))
//
// Recurrence   T(n) = 2 T(n/2) + Theta(n),  T(1) = Theta(1).
// Master Theorem: a = 2, b = 2, f(n) = Theta(n) = Theta(n^(log_b a)) = Theta(n^1), so case 2 applies and
//   T(n) = Theta(n log n).  Extra space O(n) for the merge buffer; recursion depth ceil(log2 n).
// Stability: when two items compare equal, the one from the LEFT half is taken first, so equal items keep their
// original relative order (tested). A merge of sizes p and q does at most p + q - 1 comparisons, which gives
// the checked bound: comparisons <= n * ceil(log2 n).
//
// It is a supporting algorithm here (ordering shipment and event records for reports), not part of the recovery logic.

#include <cstddef>
#include <utility>
#include <vector>

namespace scx {

struct SortStats {
    long long comparisons = 0;     // calls to `less`
    long long merges = 0;          // merge steps performed
    int maxDepth = 0;              // deepest recursion level reached
};

namespace detail {
template <typename T, typename Less>
void mergeSortRange(std::vector<T>& items, std::vector<T>& buffer, std::size_t low, std::size_t high, Less& less, SortStats& stats, int depth) {
    if (depth > stats.maxDepth) stats.maxDepth = depth;
    if (high - low < 2) return;                                  // base case: 0 or 1 element is already sorted
    const std::size_t middle = low + (high - low) / 2;
    mergeSortRange(items, buffer, low, middle, less, stats, depth + 1);      // conquer left half
    mergeSortRange(items, buffer, middle, high, less, stats, depth + 1);     // conquer right half

    ++stats.merges;
    std::size_t left = low, right = middle, out = low;
    while (left < middle && right < high) {
        ++stats.comparisons;
        // Take from the right only if it is STRICTLY smaller: ties go left, which keeps the sort stable.
        if (less(items[right], items[left])) buffer[out++] = std::move(items[right++]);
        else buffer[out++] = std::move(items[left++]);
    }
    while (left < middle) buffer[out++] = std::move(items[left++]);
    while (right < high) buffer[out++] = std::move(items[right++]);
    for (std::size_t i = low; i < high; ++i) items[i] = std::move(buffer[i]);
}
}  // namespace detail

/** Sorts `items` in place by `less` (strict weak ordering). Stable. Returns operation counts. */
template <typename T, typename Less>
SortStats mergeSort(std::vector<T>& items, Less less) {
    SortStats stats;
    std::vector<T> buffer(items.size());
    detail::mergeSortRange(items, buffer, 0, items.size(), less, stats, 0);
    return stats;
}

}  // namespace scx
