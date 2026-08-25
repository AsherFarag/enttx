#include "doctest/doctest.h"

#include <enttx/stable_id.hpp>

#include <entt/entity/entity.hpp>

#include <compare>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Custom value type + traits specialization, used to prove stable_id_value_traits
// is a genuine customization point and not hardcoded to unsigned integers.
// ---------------------------------------------------------------------------
namespace
{

struct version_value {
    int v{};
    constexpr bool operator==(const version_value&) const noexcept = default;
    constexpr auto operator<=>(const version_value&) const noexcept = default;
};

} // namespace

template<>
struct enttx::stable_id_value_traits<version_value> {
    using value_type = version_value;

    [[nodiscard]] static constexpr value_type null() noexcept { return version_value{-1}; }
    [[nodiscard]] static constexpr value_type next(value_type current) { return version_value{current.v + 1}; }
};

// Distinct phantom tags used to prove that the same underlying Value can be
// partitioned into unrelated ID domains at compile time.
namespace tags
{
struct network {};
struct undo {};
} // namespace tags

// Dependent-context helper: lets us ask "would `a == b` compile?" without
// tripping a hard (non-SFINAE) error when A and B are concrete, unrelated types.
template<typename A, typename B>
concept can_equality_compare = requires(A a, B b) { a == b; };

// Traits deliberately missing next(), used to prove the monotonic generator's
// constraint actually rejects traits that can't produce a next value.
struct no_next_traits {
    using value_type = std::uint64_t;
    [[nodiscard]] static constexpr value_type null() noexcept { return 0u; }
};

template<typename StableId>
concept has_monotonic_generator = requires { enttx::basic_monotonic_stable_id_generator<StableId>{}; };

TEST_SUITE("stable_id")
{
    // -----------------------------------------------------------------
    // stable_id_value_traits (unsigned integral specialization)
    // -----------------------------------------------------------------
    TEST_CASE("unsigned value_traits: null is zero")
    {
        CHECK(enttx::stable_id_value_traits<std::uint8_t>::null() == 0u);
        CHECK(enttx::stable_id_value_traits<std::uint16_t>::null() == 0u);
        CHECK(enttx::stable_id_value_traits<std::uint32_t>::null() == 0u);
        CHECK(enttx::stable_id_value_traits<std::uint64_t>::null() == 0u);
    }

    TEST_CASE("unsigned value_traits: next increments")
    {
        CHECK(enttx::stable_id_value_traits<std::uint64_t>::next(0u) == 1u);
        CHECK(enttx::stable_id_value_traits<std::uint64_t>::next(41u) == 42u);
        CHECK(enttx::stable_id_value_traits<std::uint32_t>::next(std::numeric_limits<std::uint32_t>::max() - 1)
              == std::numeric_limits<std::uint32_t>::max());
    }

    // -----------------------------------------------------------------
    // basic_stable_id construction
    // -----------------------------------------------------------------
    TEST_CASE("default construction yields the null value")
    {
        enttx::stable_id id{};
        CHECK(id.value == 0u);
        CHECK(id == entt::null);
    }

    TEST_CASE("explicit construction from underlying value")
    {
        enttx::stable_id id{42u};
        CHECK(id.value == 42u);
        CHECK(id != entt::null);
    }

    TEST_CASE("construction from entt::null_t yields the null value")
    {
        enttx::stable_id id{entt::null};
        CHECK(id.value == 0u);
        CHECK(id == entt::null);
    }

    TEST_CASE("constructor from Value is explicit")
    {
        CHECK(!std::is_convertible_v<std::uint64_t, enttx::stable_id>);
        CHECK(std::is_constructible_v<enttx::stable_id, std::uint64_t>);
    }

    TEST_CASE("conversion operator to Value is explicit and round-trips")
    {
        enttx::stable_id id{7u};
        CHECK(static_cast<std::uint64_t>(id) == 7u);
        CHECK(!std::is_convertible_v<enttx::stable_id, std::uint64_t>);
    }

    // -----------------------------------------------------------------
    // Equality / ordering
    // -----------------------------------------------------------------
    TEST_CASE("equality compares underlying value")
    {
        enttx::stable_id a{5u};
        enttx::stable_id b{5u};
        enttx::stable_id c{6u};

        CHECK(a == b);
        CHECK(a != c);
        CHECK_FALSE(a == c);
    }

    TEST_CASE("equality against entt::null_t only matches the null value")
    {
        enttx::stable_id null_id{};
        enttx::stable_id live_id{1u};

        CHECK(null_id == entt::null);
        CHECK_FALSE(live_id == entt::null);
        CHECK(live_id != entt::null);
    }

    TEST_CASE("three-way comparison orders by underlying value")
    {
        enttx::stable_id a{1u};
        enttx::stable_id b{2u};

        CHECK(a < b);
        CHECK(b > a);
        CHECK(a <= a);
        CHECK(a >= a);

        auto cmp = a <=> b;
        // Compared via std::is_lt rather than `cmp < 0` directly: MSVC's
        // strong_ordering::operator< against a literal 0 goes through a
        // consteval helper that requires the 0 to be seen as a genuine
        // literal, which doctest's expression-template macros don't preserve.
        CHECK(std::is_lt(cmp));
    }

    TEST_CASE("three-way comparison against entt::null_t")
    {
        enttx::stable_id null_id{};
        enttx::stable_id live_id{3u};

        CHECK(null_id <= entt::null);
        CHECK(null_id >= entt::null);
        CHECK(live_id > entt::null);

        auto cmp = live_id <=> entt::null;
        CHECK(std::is_gt(cmp));
    }

    TEST_CASE("stable_id is usable as a key in ordered containers")
    {
        std::set<enttx::stable_id> ids{enttx::stable_id{3u}, enttx::stable_id{1u}, enttx::stable_id{2u}};
        REQUIRE(ids.size() == 3u);

        auto it = ids.begin();
        CHECK(static_cast<std::uint64_t>(*it++) == 1u);
        CHECK(static_cast<std::uint64_t>(*it++) == 2u);
        CHECK(static_cast<std::uint64_t>(*it++) == 3u);
    }

    // -----------------------------------------------------------------
    // Tag-based domain separation
    // -----------------------------------------------------------------
    TEST_CASE("distinct tags produce distinct, unrelated types")
    {
        using network_id = enttx::basic_stable_id<std::uint64_t, tags::network>;
        using undo_id = enttx::basic_stable_id<std::uint64_t, tags::undo>;

        CHECK_FALSE(std::is_same_v<network_id, undo_id>);
        // Neither type should be constructible from, or comparable with, the other.
        CHECK_FALSE(std::is_constructible_v<network_id, undo_id>);
        CHECK_FALSE(can_equality_compare<network_id, undo_id>);
    }

    TEST_CASE("tagged ids still behave like ordinary stable ids")
    {
        using network_id = enttx::basic_stable_id<std::uint64_t, tags::network>;

        network_id id{99u};
        CHECK(id.value == 99u);
        CHECK(id != entt::null);
    }

    // -----------------------------------------------------------------
    // Different underlying value types
    // -----------------------------------------------------------------
    TEST_CASE_TEMPLATE("works across unsigned integral widths", Value, std::uint8_t, std::uint16_t, std::uint32_t,
                        std::uint64_t)
    {
        using id_type = enttx::basic_stable_id<Value>;

        id_type default_id{};
        CHECK(default_id.value == static_cast<Value>(0));
        CHECK(default_id == entt::null);

        id_type id{static_cast<Value>(5)};
        CHECK(id.value == static_cast<Value>(5));
        CHECK(id != default_id);

        CHECK(std::is_trivially_copyable_v<id_type>);
        CHECK(std::is_standard_layout_v<id_type>);
    }

    // -----------------------------------------------------------------
    // Custom (non-integral) value type via the traits customization point
    // -----------------------------------------------------------------
    TEST_CASE("customization point supports non-integral Value types")
    {
        using version_id = enttx::basic_stable_id<version_value>;

        version_id default_id{};
        CHECK(default_id.value == version_value{-1});
        CHECK(default_id == entt::null);

        version_id id{version_value{0}};
        CHECK(id.value == version_value{0});
        CHECK(id != entt::null);
    }

    // -----------------------------------------------------------------
    // basic_monotonic_stable_id_generator
    // -----------------------------------------------------------------
    TEST_CASE("monotonic generator starts at next(null) and increments")
    {
        enttx::monotonic_stable_id_generator gen{};

        auto first = gen();
        auto second = gen();
        auto third = gen();

        CHECK(static_cast<std::uint64_t>(first) == 1u);
        CHECK(static_cast<std::uint64_t>(second) == 2u);
        CHECK(static_cast<std::uint64_t>(third) == 3u);
    }

    TEST_CASE("monotonic generator never produces the null id")
    {
        enttx::monotonic_stable_id_generator gen{};

        for (int i = 0; i < 100; ++i) {
            CHECK(gen() != entt::null);
        }
    }

    TEST_CASE("monotonic generator produces strictly increasing, unique ids")
    {
        enttx::monotonic_stable_id_generator gen{};

        std::unordered_set<std::uint64_t> seen;
        enttx::stable_id previous{};

        for (int i = 0; i < 1000; ++i) {
            auto id = gen();
            CHECK(id > previous);
            CHECK(seen.insert(static_cast<std::uint64_t>(id)).second);
            previous = id;
        }
    }

    TEST_CASE("independent generator instances track state independently")
    {
        enttx::monotonic_stable_id_generator gen_a{};
        enttx::monotonic_stable_id_generator gen_b{};

        [[maybe_unused]] auto a_first = gen_a();
        [[maybe_unused]] auto a_second = gen_a();
        auto a_third = gen_a();

        auto b_first = gen_b();

        CHECK(static_cast<std::uint64_t>(a_third) == 3u);
        CHECK(static_cast<std::uint64_t>(b_first) == 1u);
    }

    TEST_CASE("monotonic generator works with a custom Value width")
    {
        using id8 = enttx::basic_stable_id<std::uint8_t>;
        enttx::basic_monotonic_stable_id_generator<id8> gen{};

        CHECK(static_cast<std::uint8_t>(gen()) == 1u);
        CHECK(static_cast<std::uint8_t>(gen()) == 2u);
    }

    TEST_CASE("monotonic generator works with a custom (non-integral) traits type")
    {
        using version_id = enttx::basic_stable_id<version_value>;
        enttx::basic_monotonic_stable_id_generator<version_id> gen{};

        CHECK(gen().value == version_value{0});
        CHECK(gen().value == version_value{1});
    }

    TEST_CASE("monotonic generator requires value_traits::next to be well-formed")
    {
        using no_next_id = enttx::basic_stable_id<std::uint64_t, void, no_next_traits>;

        // stable_id_value_traits for unsigned integrals provides next(), so the
        // generator is instantiable...
        CHECK(has_monotonic_generator<enttx::stable_id>);
        // ...but traits without next() must be rejected at compile time rather
        // than producing a generator that can't generate anything.
        CHECK_FALSE(has_monotonic_generator<no_next_id>);
    }

    // -----------------------------------------------------------------
    // basic_random_stable_id_generator (splitmix-based)
    // -----------------------------------------------------------------
    TEST_CASE("random generator never produces the null id")
    {
        enttx::random_stable_id_generator gen{1234u};

        for (int i = 0; i < 10000; ++i) {
            CHECK(gen() != entt::null);
        }
    }

    TEST_CASE("random generator is deterministic for a given seed")
    {
        enttx::random_stable_id_generator gen_a{9001u};
        enttx::random_stable_id_generator gen_b{9001u};

        for (int i = 0; i < 100; ++i) {
            CHECK(gen_a() == gen_b());
        }
    }

    TEST_CASE("random generator with different seeds diverges")
    {
        enttx::random_stable_id_generator gen_a{1u};
        enttx::random_stable_id_generator gen_b{2u};

        bool any_different = false;
        for (int i = 0; i < 16; ++i) {
            if (gen_a() != gen_b()) {
                any_different = true;
                break;
            }
        }
        CHECK(any_different);
    }

    TEST_CASE("random generator produces well-distributed (non-repeating) values")
    {
        enttx::random_stable_id_generator gen{42u};

        std::unordered_set<std::uint64_t> seen;
        constexpr int draws = 5000;

        for (int i = 0; i < draws; ++i) {
            seen.insert(static_cast<std::uint64_t>(gen()));
        }

        // With a 64-bit splitmix stream, collisions across 5000 draws should be
        // vanishingly unlikely; a low unique count would indicate a broken PRNG.
        CHECK(seen.size() == draws);
    }

    TEST_CASE("reseeding resets the generator's stream")
    {
        enttx::random_stable_id_generator gen{555u};

        auto first_run_a = gen();
        auto first_run_b = gen();

        gen.seed(555u);

        auto second_run_a = gen();
        auto second_run_b = gen();

        CHECK(first_run_a == second_run_a);
        CHECK(first_run_b == second_run_b);
    }

    TEST_CASE("random generator works with a 32-bit Value")
    {
        using id32 = enttx::basic_stable_id<std::uint32_t>;
        enttx::basic_random_stable_id_generator<id32> gen{7u};

        for (int i = 0; i < 1000; ++i) {
            CHECK(gen() != entt::null);
        }
    }

    // -----------------------------------------------------------------
    // std::hash specialization
    // -----------------------------------------------------------------
    TEST_CASE("std::hash is consistent with equality")
    {
        enttx::stable_id a{10u};
        enttx::stable_id b{10u};
        enttx::stable_id c{11u};

        std::hash<enttx::stable_id> hasher{};

        CHECK(hasher(a) == hasher(b));
        // Not a strict guarantee in general, but true for identity-style hashing
        // over a small domain and useful as a sanity check here.
        CHECK(hasher(a) != hasher(c));
    }

    TEST_CASE("stable_id works as a key in unordered associative containers")
    {
        std::unordered_map<enttx::stable_id, std::string> map;

        map[enttx::stable_id{1u}] = "one";
        map[enttx::stable_id{2u}] = "two";
        map[enttx::stable_id{1u}] = "uno"; // overwrite

        REQUIRE(map.size() == 2u);
        CHECK(map[enttx::stable_id{1u}] == "uno");
        CHECK(map[enttx::stable_id{2u}] == "two");

        std::unordered_set<enttx::stable_id> set;
        enttx::monotonic_stable_id_generator gen{};
        for (int i = 0; i < 50; ++i) {
            set.insert(gen());
        }
        CHECK(set.size() == 50u);
    }

    // -----------------------------------------------------------------
    // Alias sanity
    // -----------------------------------------------------------------
    TEST_CASE("public aliases resolve to the documented defaults")
    {
        CHECK(std::is_same_v<enttx::stable_id, enttx::basic_stable_id<std::uint64_t>>);
        CHECK(std::is_same_v<enttx::monotonic_stable_id_generator,
                              enttx::basic_monotonic_stable_id_generator<enttx::stable_id>>);
        CHECK(std::is_same_v<enttx::random_stable_id_generator,
                              enttx::basic_random_stable_id_generator<enttx::stable_id>>);
    }
}
