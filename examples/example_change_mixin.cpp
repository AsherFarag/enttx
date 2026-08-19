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
	reg.emplace<transform>( entt, 0.f, 0.f );
    
	auto observer = enttx::observe<transform>( reg );

	reg.patch<transform>( entt, []( transform& t ) { t.x = 10.f; } );
	reg.patch<transform>( entt, []( transform& t ) { t.y = 5.f; } );

	observer.disconnect();

	enttx::commit commit;
	observer.collect( commit );

	std::cout << "Before commit apply: " << reg.get<transform>( entt ) << std::endl;

	auto inverted = commit.invert();
	inverted.apply( reg );
	std::cout << "After inverted commit apply: " << reg.get<transform>( entt ) << std::endl;

	commit.apply( reg );
	std::cout << "After commit apply: " << reg.get<transform>( entt ) << std::endl;
}