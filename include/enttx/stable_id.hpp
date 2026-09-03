/*!
 * @file stable_id.hpp
 * @brief Provides stable identifiers and generators for use with EnTT.
 */

#pragma once
#include "core.hpp"

#include <entt/entity/entity.hpp>
#include <entt/stl/concepts.hpp>
#include <entt/stl/cstdint.hpp>
#include <entt/stl/type_traits.hpp>

namespace enttx {

/**
 * @brief Customization point for stable identifier value types.
 *
 * A specialization must provide:
 *
 * using value_type = ...;
 * static constexpr value_type null() noexcept;
 *
 * optional: static constexpr value_type next(value_type);
 */
template <typename> struct stable_id_value_traits;

template <typename Value, typename = void,
          typename = stable_id_value_traits<Value>>
struct basic_stable_id;

template <typename> struct basic_monotonic_stable_id_generator;

template <typename> struct basic_random_stable_id_generator;

/*! @brief Alias declaration for the most common use case. */
using stable_id = basic_stable_id<stl::uint64_t>;

/*! @brief Alias declaration for the most common use case. */
using monotonic_stable_id_generator =
    basic_monotonic_stable_id_generator<stable_id>;

/*! @brief Alias declaration for the most common use case. */
using random_stable_id_generator = basic_random_stable_id_generator<stable_id>;

template <stl::unsigned_integral Value> struct stable_id_value_traits<Value> {
  /*! @brief Underlying value type of the stable identifier. */
  using value_type = Value;

  [[nodiscard]]
  static constexpr value_type null() noexcept {
    return static_cast<Value>(0);
  }

  [[nodiscard]]
  static constexpr value_type next(value_type current) {
    return ++current;
  }
};

/*!
 * @brief Basic stable identifier.
 * @tparam Value Underlying value type of the stable identifier.
 * @tparam Tag Tag type to differentiate stable identifiers of the same
 * underlying value type.
 */
template <typename Value, typename Tag, typename ValueTraits>
  requires stl::is_same_v<Value, typename ValueTraits::value_type>
struct basic_stable_id<Value, Tag, ValueTraits> {
  /*! @brief Underlying value type of the stable identifier. */
  using value_type = Value;
  /*! @brief Traits for the underlying value type of the stable identifier. */
  using value_traits = ValueTraits;
  /*! @brief Tag type to differentiate stable identifiers of the same underlying
   * value type. */
  using tag_type = Tag;

  /*! @brief Underlying value of the stable identifier. */
  Value value;

  constexpr basic_stable_id() noexcept : value{value_traits::null()} {}
  constexpr explicit basic_stable_id(Value v) noexcept : value(v) {}
  constexpr basic_stable_id(entt::null_t) noexcept
      : value{value_traits::null()} {}

  constexpr explicit operator Value() const noexcept { return value; }

  constexpr bool operator==(const basic_stable_id &) const noexcept = default;
  constexpr bool operator==(entt::null_t) const noexcept {
    return value == value_traits::null();
  }

  constexpr auto operator<=>(const basic_stable_id &) const noexcept = default;
  constexpr auto operator<=>(entt::null_t) const noexcept {
    return value <=> value_traits::null();
  }
};

/*!
 * @brief Generates stable identifiers in a monotonic fashion.
 * @tparam StableId Type of stable identifier to generate.
 * @remark Each call to the generator will produce a new stable identifier that
 * is value_traits::next() of the previous one.
 */
template <typename StableId>
  requires requires(typename StableId::value_type v) {
    {
      StableId::value_traits::next(v)
    } -> stl::same_as<typename StableId::value_type>;
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
  constexpr stable_id_type operator()() {
    return stable_id_type{current = value_traits::next(current)};
  }
};

namespace internal {

template <stl::unsigned_integral Value> struct splitmix;

template <> struct splitmix<stl::uint32_t> {
  using result_type = stl::uint32_t;

  result_type state;

  [[nodiscard]] constexpr result_type operator()() noexcept {
    stl::uint32_t z = (state += 0x9E3779B9u);

    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;

    return z ^ (z >> 16);
  }
};

template <> struct splitmix<stl::uint64_t> {
  using result_type = stl::uint64_t;

  result_type state;

  [[nodiscard]] constexpr result_type operator()() noexcept {
    stl::uint64_t z = (state += 0x9E3779B97F4A7C15ull);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;

    return z ^ (z >> 31);
  }
};

} // namespace internal

/*!
 * @brief Generates stable identifiers via a pseudo-random number generator
 * using the splitmix algorithm.
 * @tparam StableId Type of stable identifier to generate.
 * @warning This generator is not cryptographically secure and should not be
 * used for security purposes.
 * @remark The underlying value type should be 64 bits or more to ensure a good
 * distribution of generated values. Lower bit widths may result in a poor
 * distribution of generated values and an increased likelihood of collisions.
 *
 * @code .cpp
 * enttx::random_stable_id_generator gen{stl::random_device{}()};
 * @endcode
 */
template <typename StableId>
  requires stl::unsigned_integral<typename StableId::value_type>
struct basic_random_stable_id_generator<StableId> {
  /*! @brief Underlying value type of the stable identifier. */
  using value_type = typename StableId::value_type;
  /*! @brief Traits for the underlying value type of the stable identifier. */
  using value_traits = typename StableId::value_traits;
  /*! @brief Stable identifier type. */
  using stable_id_type = StableId;

public:
  constexpr basic_random_stable_id_generator(value_type seed) noexcept
      : generator{seed} {}

  constexpr void seed(value_type new_seed) noexcept {
    generator.state = new_seed;
  }

  [[nodiscard]]
  constexpr stable_id_type operator()() noexcept {
    value_type value;
    do {
      value = generator();
    } while (value == value_traits::null());
    return stable_id_type{value};
  }

private:
  internal::splitmix<value_type> generator;
};

} // namespace enttx

namespace std {
template <typename Value, typename ValueTraits, typename Tag>
struct hash<enttx::basic_stable_id<Value, ValueTraits, Tag>> {
  [[nodiscard]]
  size_t operator()(const enttx::basic_stable_id<Value, ValueTraits, Tag> &id)
      const noexcept {
    return std::hash<Value>{}(id.value);
  }
};
} // namespace std