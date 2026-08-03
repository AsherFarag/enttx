#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <functional>

namespace enttx {

    /**
     * @brief Stable 128-bit identity for a single node (root or child) inside
     * a prefab's authored hierarchy.
     *
     * Assigned once when a node is authored and never regenerated afterwards.
     * This is what lets a derived prefab target one *specific* inherited
     * child for override or deletion, instead of only ever being able to
     * append new siblings. Distinct from `prefab_id`, which identifies a
     * whole prefab *asset*.
     */
    struct persistent_id {
        std::array<std::uint8_t, 16> value{};

        friend bool operator==(const persistent_id&, const persistent_id&) = default;

        /*! @brief True for a default-constructed / never-assigned id. */
        [[nodiscard]] bool is_null() const noexcept {
            return value == std::array<std::uint8_t, 16>{};
        }
    };

    /**
     * @brief Generates a new random 128-bit id.
     *
     * Deliberately not constexpr/deterministic - persistent_ids need to be
     * unique per authored node, not reproducible from source location or
     * call order.
     */
    [[nodiscard]]
    inline persistent_id generate_persistent_id() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        static thread_local std::uniform_int_distribution<std::uint64_t> dist;

        persistent_id id;
        const std::uint64_t lo = dist(rng);
        const std::uint64_t hi = dist(rng);
        std::memcpy(id.value.data(), &lo, sizeof(lo));
        std::memcpy(id.value.data() + sizeof(lo), &hi, sizeof(hi));
        return id;
    }

}

template<>
struct std::hash<enttx::persistent_id> {
    std::size_t operator()(const enttx::persistent_id& id) const noexcept {
        // NOTE: std::bit_cast<std::uint64_t[2]>(...) does not compile - a
        // function cannot return an array type. memcpy into two scalars
        // instead.
        std::uint64_t lo, hi;
        std::memcpy(&lo, id.value.data(), sizeof(lo));
        std::memcpy(&hi, id.value.data() + sizeof(lo), sizeof(hi));
        return std::hash<std::uint64_t>{}(lo) ^ (std::hash<std::uint64_t>{}(hi) << 1);
    }
};
