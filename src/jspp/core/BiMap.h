#pragma once
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace jspp::internal {

/**
 * @brief A small bidirectional map keeping a 1:1 relation between two key
 *        types. Both directions look up in O(1).
 * @note  Values must be hashable and copyable; entries are never overwritten,
 *        insert() fails if either side is already bound.
 */
template <typename A, typename B>
class BiMap {
public:
    /** @return true if the pair was inserted; false if either side is bound. */
    bool insert(A const& a, B const& b) {
        if (forward_.contains(a) || backward_.contains(b)) {
            return false;
        }
        forward_.emplace(a, b);
        backward_.emplace(b, a);
        return true;
    }

    /** @return Pointer to the mapped value, or nullptr when absent. */
    [[nodiscard]] B const* find(A const& a) const {
        auto iter = forward_.find(a);
        return iter == forward_.end() ? nullptr : &iter->second;
    }

    /** @return Pointer to the mapped key, or nullptr when absent. */
    [[nodiscard]] A const* findValue(B const& b) const {
        auto iter = backward_.find(b);
        return iter == backward_.end() ? nullptr : &iter->second;
    }

    [[nodiscard]] bool contains(A const& a) const { return forward_.contains(a); }

    [[nodiscard]] bool containsValue(B const& b) const { return backward_.contains(b); }

    /** @return Mapped value; throws std::out_of_range when absent (map::at semantics). */
    [[nodiscard]] B const& at(A const& a) const { return forward_.at(a); }

    [[nodiscard]] size_t size() const { return forward_.size(); }

    void clear() {
        forward_.clear();
        backward_.clear();
    }

    /** @brief Visit every (key, value) pair. */
    template <typename F>
    void forEach(F&& fn) const {
        for (auto const& [a, b] : forward_) {
            fn(a, b);
        }
    }

private:
    std::unordered_map<A, B> forward_{};
    std::unordered_map<B, A> backward_{};
};

} // namespace jspp::internal
