#include <enttx/stable_id.hpp>

#include <entt/entity/registry.hpp>

#include <iostream>
#include <string>
#include <random> // guid_generator

struct guid {
	std::uint64_t hi{}, lo{};

	bool operator==( const guid& ) const = default;

	[[nodiscard]]
	std::string to_string() const {
		return std::to_string(hi) + "-" + std::to_string(lo);
	}
};

struct guid_generator {
	std::random_device rd;
	std::mt19937_64 eng{ rd() };
	std::uniform_int_distribution<std::uint64_t> dist;
	guid operator()() {
		return guid{ dist( eng ), dist( eng ) };
	}
};

template<>
struct enttx::stable_id_value_traits<guid> {
	using value_type = guid;
	using stable_id_type = enttx::basic_stable_id<guid>;

	static constexpr value_type null() { return guid{ 0, 0 }; }
	// next() only needed if used with basic_monotonic_stable_id_generator
};

void guid_example() {
	guid_generator gen{};
	entt::registry reg;

	for ( int i = 0; i < 10; ++i )
	{
		reg.emplace<enttx::basic_stable_id<guid>>( reg.create(), gen() );
	}

	for ( const auto [entity, guid] : reg.view<enttx::basic_stable_id<guid>>().each() )
	{
		std::cout << "Entity: " << entt::to_entity( entity ) << " has GUID: " << guid.value.to_string() << "\n";
	}
}

void registry_example() {
	enttx::monotonic_stable_id_generator gen{};
	entt::registry reg;

	// Create a mapping from stable_id to entt::entity for quick lookup and store it in the registry's context.
	using stable_id_map = std::unordered_map<enttx::stable_id, entt::entity>;
	stable_id_map& id_map = reg.ctx().emplace<stable_id_map>();

	// When a stable_id component is constructed, add it to the map stored in the registry's context.
	reg.on_construct<enttx::stable_id>().connect<
		[]( entt::registry& reg, entt::entity e )
		{
			const auto& id = reg.get<enttx::stable_id>( e );
			reg.ctx().get<stable_id_map>().emplace( id, e );
		}
	>();

	// When a stable_id component is destroyed, remove it from the map stored in the registry's context.
	reg.on_destroy<enttx::stable_id>().connect<
		[]( entt::registry& reg, entt::entity e )
		{
			const auto& id = reg.get<enttx::stable_id>( e );
			reg.ctx().get<stable_id_map>().erase( id );
		}
	>();

	// Generate entities with stable_id components
	for ( int i = 0; i < 10; ++i )
	{
		reg.emplace<enttx::stable_id>( reg.create(), gen() );
	}

	// Example stable_id to search for
	const enttx::stable_id search_id{ 5 };

	// Look up the entity associated with the stable_id in the map stored in the registry's context.
	const entt::entity found_entity = id_map.contains( search_id ) ? id_map.at( search_id ) : entt::null;

	if ( found_entity != entt::null && reg.valid( found_entity ) )
	{
		std::cout << "Found entity: " << entt::to_entity( found_entity ) << " with stable_id: " << search_id.value << "\n";
	}
	else
	{
		std::cout << "Entity with stable_id: " << search_id.value << " not found.\n";
		return;
	}

	// Destroy the found entity (this will also remove it from the map due to the on_destroy signal).
	reg.destroy( found_entity );

	// Verify that the entity is no longer in the map.
	const entt::entity after_destroy_entity = id_map.contains( search_id ) ? id_map.at( search_id ) : entt::null;

	if ( after_destroy_entity == entt::null )
	{
		std::cout << "Entity with stable_id: " << search_id.value << " has been destroyed and removed from the map.\n";
	}
	else
	{
		std::cout << "Entity with stable_id: " << search_id.value << " still exists in the map after destruction.\n";
	}
}

int main()
{
	std::cout << "=== Monotonic Generation Example ===\n";
    {
        enttx::monotonic_stable_id_generator gen{};
        for ( int i = 0; i < 10; ++i )
        {
            auto id = gen();
            std::cout << "Generated ID: " << id.value << "\n";
        }
    }
	std::cout << std::endl;

	std::cout << "=== Random Generation Example ===\n";
	{
		enttx::random_stable_id_generator gen{ std::random_device{}() };
		for ( int i = 0; i < 10; ++i )
		{
			auto id = gen();
			std::cout << "Generated ID: " << id.value << "\n";
		}
	}
    std::cout << std::endl;

	std::cout << "=== Custom GUID Example ===\n";
	guid_example();
	std::cout << std::endl;

	std::cout << "=== Find entity by stable_id ===\n";
	registry_example();
	std::cout << std::endl;

    // Wait for user input before exiting.
    std::cout << "Press Enter to exit...";
    std::cin.get();
}