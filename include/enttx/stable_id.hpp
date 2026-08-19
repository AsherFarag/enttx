/*!
 * @file stable_id.hpp
 * @brief Provides stable identifiers and generators for use with EnTT.
 */

#pragma once
#include "config.hpp"
#include <entt/entity/entity.hpp> // For entt::null and entt::null_t
#include <cstdint>
#include <concepts>

namespace enttx
{

template<typename, typename = void>
struct basic_stable_id;

template<typename>
struct basic_monotonic_stable_id_generator;

template<typename>
struct basic_random_stable_id_generator;

/*! @brief Alias declaration for the most common use case. */
using stable_id = basic_stable_id<std::uint64_t>;

/*! @brief Alias declaration for the most common use case. */
using monotonic_stable_id_generator = basic_monotonic_stable_id_generator<stable_id>;

/*! @brief Alias declaration for the most common use case. */
using random_stable_id_generator = basic_random_stable_id_generator<stable_id>;

/*! @brief */
template<typename Value>
struct stable_id_value_traits;

template<std::integral Value>
struct stable_id_value_traits<Value> {
    /*! @brief Underlying value type of the stable identifier. */
    using value_type = Value;

    [[nodiscard]]
    static constexpr value_type null() noexcept { return static_cast<Value>(0); }

    [[nodiscard]]
    static constexpr value_type next(value_type current) { return ++current; }
};

/*! @brief */
template<typename Value, typename Tag>
struct basic_stable_id {
    /*! @brief Underlying value type of the stable identifier. */
    using value_type = Value;
    /*! @brief Tag type to differentiate stable identifiers of the same underlying value type. */
    using tag_type = Tag;
    /*! @brief Traits for the underlying value type of the stable identifier. */
    using value_traits = stable_id_value_traits<Value>;

    Value value;

    constexpr basic_stable_id() noexcept : value{value_traits::null()} {}
    constexpr explicit basic_stable_id(Value v) noexcept : value(v) {}
    constexpr basic_stable_id(entt::null_t) noexcept : value{value_traits::null()} {}

    constexpr explicit operator Value() const noexcept { return value; }

    constexpr bool operator==(const basic_stable_id&) const noexcept = default;
    constexpr bool operator==(entt::null_t) const noexcept { return value == value_traits::null(); }
};

/*! @brief */
template<typename StableId>
requires requires(typename StableId::value_type v) {
    { typename StableId::value_traits::next(v) } -> std::same_as<typename StableId::value_type>;
}
struct basic_monotonic_stable_id_generator<StableId> {
    /*! @brief Underlying value type of the stable identifier. */
    using value_type = typename StableId::value_type;
    /*! @brief Traits for the underlying value type of the stable identifier. */
    using value_traits = typename StableId::value_traits;
    /*! @brief Stable identifier type. */
    using stable_id_type = StableId;

    /*! @brief Current value of the generator. */
    value_type current{value_traits::null()};

    [[nodiscard]]
    stable_id_type operator()() {
        return stable_id_type{ current = value_traits::next(current) };
    }
};

namespace internal 
{
struct splitmix_u32 { 
};

struct splitmix_u64 {
};
} // namespace internal

/*! @brief */
template<typename StableId>
struct basic_random_stable_id_generator {
    /*! @brief Underlying value type of the stable identifier. */
    using value_type = typename StableId::value_type;
    /*! @brief Traits for the underlying value type of the stable identifier. */
    using value_traits = typename StableId::value_traits;
    /*! @brief Stable identifier type. */
    using stable_id_type = StableId;

    value_type state{ value_traits::null() };

    [[nodiscard]]
    stable_id_type operator()() noexcept {
        value_type r = value_traits::null();
        return stable_id_type{r};
    }
};

} // namespace enttx

namespace std 
{
template<typename Value, typename Tag>
struct hash<enttx::basic_stable_id<Value, Tag>> {
	[[nodiscard]]
	size_t operator()(const enttx::basic_stable_id<Value, Tag>& id) const noexcept {
		return std::hash<Value>{}(id.value);
	}
};
} // namespace std