#include "doctest/doctest.h"

#include <enttx/observer.hpp>
#include <enttx/change_mixin.hpp>


#include <entt/entt.hpp>

#include <vector>
#include <cstddef>
#include <cstring>

// ============================================================================
// Component Definitions & Storage Setup
// ============================================================================

struct Position {
    float x, y;
    bool operator==(const Position&) const = default;
};

std::ostream &operator<<(std::ostream &os, const Position& pos) {
    return os << '(' << pos.x << ", " << pos.y << ')';
}

std::istream &operator>>(std::istream &is, Position& pos) {
    char open{};
    char comma{};
    char close{};

    if (is >> open >> pos.x >> comma >> pos.y >> close;
        open == '(' && comma == ',' && close == ')') {
        return is;
    }

    is.setstate(std::ios::failbit);
    return is;
}

struct Tag {};

// Configure EnTT to use enttx::change_mixin for the components
template<>
struct entt::storage_type<Position> {
    using type = enttx::change_mixin<entt::basic_storage<Position>>;
};

template<>
struct entt::storage_type<Tag> {
    using type = enttx::change_mixin<entt::basic_storage<Tag>>;
};

// ============================================================================
// Mock Archive for Serialization Testing
// ============================================================================

struct output_text_archive
{
    std::ostringstream oss;
    template<typename T>
    void operator()( const T& value )
    {
        oss << value << std::endl;
    }

    void operator()( const std::uint8_t value )
    {
        oss << (std::uint32_t)value << std::endl;
    }
    void operator()( const entt::entity value )
    {
        oss << entt::to_integral( value ) << std::endl;
    }

    template<typename T, typename... Rest>
    void operator()( const T& value, const Rest&... rest )
    {
        operator()( value );
        operator()( rest... );
    }
};

struct input_text_archive
{
    std::istringstream iss;
    template<typename T>
    void operator()( T& value )
    {
        iss >> value;
    }

    void operator()( std::uint8_t& value )
    {
        std::uint32_t temp;
        iss >> temp;
        value = static_cast<std::uint8_t>( temp );
    }

    void operator()( entt::entity& value )
    {
        std::uint32_t temp;
        iss >> temp;
        value = entt::entity{ temp };
    }

    template<typename T, typename... Rest>
    void operator()( T& value, Rest&... rest )
    {
        operator()( value );
        operator()( rest... );
    }
};

// ============================================================================
// Test Cases
// ============================================================================

TEST_CASE("Observer: Stateful Component Lifecycle and Application") {
    entt::registry reg;
    auto e = reg.create();
    auto obs = enttx::observe<Position>(reg);
    
    SUBCASE("Collect clears the internal changes queue") {
        reg.emplace<Position>(e, 1.0f, 2.0f);
        
        enttx::commit commit;
        obs->collect(commit);
        
        // Second collect should be empty
        enttx::commit empty_commit;
        obs->collect(empty_commit);
        
        entt::registry test_reg;
        auto e_test = test_reg.create(e);
        empty_commit.apply(test_reg);
        CHECK_FALSE(test_reg.all_of<Position>(e_test));
    }
    
    SUBCASE("Apply updates accurately") {
        reg.emplace<Position>(e, 1.0f, 2.0f);
        reg.patch<Position>(e, [](auto& p) { p.x = 42.0f; });
        reg.erase<Position>(e);
        
        enttx::commit commit;
        obs->collect(commit);
        
        // Applying the full lifecycle to a new entity should result in no component
        entt::registry reg2;
        auto e2 = reg2.create(e);
        commit.apply(reg2);
        
        CHECK_FALSE(reg2.all_of<Position>(e2));
    }
}

TEST_CASE("Observer: Commit Inversion") {
    entt::registry reg;
    auto e = reg.create();
    auto obs = enttx::observe<Position>(reg);

    reg.emplace<Position>(e, 10.0f, 20.0f);
    reg.patch<Position>(e, [](auto& p) { p.x = 99.0f; });

    enttx::commit commit;
    obs->collect(commit);

    auto inverted = commit.invert();

    // Setup target registry mimicking the state *after* the original commit
    entt::registry target;
    auto target_e = target.create(e);
    target.emplace<Position>(target_e, 99.0f, 20.0f);

    // Apply inversion
    inverted.apply(target);

    // The inverted update should restore it to 10.0f, and inverted construct should erase it.
    CHECK_FALSE(target.all_of<Position>(target_e));
}

TEST_CASE("Observer: Snapshot and Loader Serialization") {
    entt::registry reg;
    auto e1 = reg.create();
    auto e2 = reg.create();

    auto obs_pos = enttx::observe<Position>(reg);
    auto obs_tag = enttx::observe<Tag>(reg);

    // Generate some changes
    reg.emplace<Position>(e1, 3.14f, 2.71f);
    reg.patch<Position>(e1, [](auto& p) { p.x = 0.0f; });
    reg.emplace<Tag>(e2);
    reg.erase<Tag>(e2);

    enttx::commit commit;
    obs_pos->collect(commit);
    obs_tag->collect(commit);

    // 1. Serialize using commit_snapshot
    output_text_archive oarchive;
    
    enttx::commit_snapshot snapshot(commit);
    snapshot.get<Position>(oarchive);
    snapshot.get<Tag>(oarchive);

    CHECK(oarchive.oss.str().size() > 0);

    // 2. Deserialize using commit_loader
    input_text_archive iarchive;
    iarchive.iss.str(oarchive.oss.str());

    enttx::commit loaded_commit;
    enttx::commit_loader loader(loaded_commit);
    
    loader.get<Position>(iarchive);
    loader.get<Tag>(iarchive);

    // 3. Verify the loaded commit applies correctly
    entt::registry test_reg;
    auto t_e1 = test_reg.create(e1);
    auto t_e2 = test_reg.create(e2);

    loaded_commit.apply(test_reg);

    // Verify Position: Emplaced then Patched -> Should exist and be patched
    CHECK(test_reg.all_of<Position>(t_e1));
    CHECK(test_reg.get<Position>(t_e1).x == 0.0f);
    CHECK(test_reg.get<Position>(t_e1).y == 2.71f);

    // Verify Tag: Emplaced then Erased -> Should not exist
    CHECK_FALSE(test_reg.all_of<Tag>(t_e2));
}

TEST_CASE("Observer: Entity Remapping") {
    entt::registry reg;
    
    auto e1 = reg.create();
    auto e2 = reg.create(); // Mapped destination
    
    auto obs = enttx::observe<Position>(reg);
    reg.emplace<Position>(e1, 7.0f, 7.0f);
    
    enttx::commit commit;
    obs->collect(commit);
    
    enttx::entity_remap remap;
    remap.map(e1, e2);
    
    commit.apply(reg, &remap);
    
    CHECK(reg.all_of<Position>(e2));
    CHECK(reg.get<Position>(e2).x == 7.0f);
}

TEST_CASE("Observer: Connect and Disconnect") {
    entt::registry reg;
    auto e = reg.create();
    auto obs = enttx::observe<Position>(reg);
    
    obs->disconnect();
    
    // This should not be recorded
    reg.emplace<Position>(e, 0.0f, 0.0f);
    
    enttx::commit commit;
    obs->collect(commit);
    
    entt::registry test_reg;
    auto e_test = test_reg.create(e);
    commit.apply(test_reg);
    
    CHECK_FALSE(test_reg.all_of<Position>(e_test));
}