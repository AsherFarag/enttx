#include <enttx/change_mixin.hpp>
#include <enttx/observer.hpp>
#include <entt/entity/registry.hpp>

#include <deque>
#include <iostream>
#include <string>

struct transform {
    float x{};
    float y{};
};

std::ostream &operator<<(std::ostream &os, const transform &t) {
    return os << "(" << t.x << ", " << t.y << ")";
}

template<>
struct entt::storage_type<transform> {
    using type = enttx::change_mixin<entt::basic_storage<transform>>;
};

int main()
{
	entt::registry reg;

	const auto entt = reg.create();
	auto observer = enttx::observe<transform>( reg );

	reg.emplace<transform>( entt, 0.f, 0.f );
	reg.patch<transform>( entt, []( transform& t ) { t.x = 10.f; } );
	reg.patch<transform>( entt, []( transform& t ) { t.y = 5.f; } );

	enttx::commit commit{};
	observer.collect( commit );

	enttx::entity_remap remap{};
	remap.map( entt, reg.create() );

	commit.apply( reg, &remap );

	std::cout << "Remapped entity transform: " << reg.get<transform>( remap( entt ) ) << std::endl;

	commit.invert().apply( reg, &remap );

	std::cout << "Remapped entity transform: " << reg.any_of<transform>( remap( entt ) ) << std::endl;
}