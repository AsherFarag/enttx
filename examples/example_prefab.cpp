#include <concepts>
#include <functional>
#include <cstdio>
#include <chrono>

#include <enttx/prefab.hpp>

using namespace entt::literals;
using namespace enttx;

// --- example components -----------------------------------------------

struct transform {
    float x = 0, y = 0, z = 0;
};

struct name_tag {
    const char* value = "";
};

struct health {
    int hp = 100;
};

struct damage {
	int amount = 0;
};

// A component that references another node within the same prefab -
// demonstrates the remap_traits customization point.
struct mount_socket {
    entt::entity attachment = entt::null;

	static void remap( entt::registry& reg, entt::entity e, const entity_remap& remap ) {
        auto& socket = reg.get<mount_socket>( e );
        socket.attachment = remap.translate( socket.attachment );
	}
};

int main() {

	prefab_registry prefabs;

    const prefab_id sword_prefab = "sword"_hs;
    const node_id   sword_root   = prefabs.create_prefab(sword_prefab);
    prefab_builder{prefabs, sword_prefab, sword_root}
        .emplace<name_tag>("Sword")
        .emplace<transform>(0.f, 0.f, 0.f)
        .emplace<damage>(50);

	const prefab_id player_prefab = "player"_hs;
    const node_id   player_root   = prefabs.create_prefab(player_prefab);
    prefab_builder{prefabs, player_prefab, player_root}
        .emplace<name_tag>("Player")
        .emplace<transform>(0.f, 0.f, 0.f)
        .emplace<health>(100)
        .add_nested(sword_prefab)
        .add_child([](prefab_builder& b) {
            b.emplace<name_tag>("Left Hand")
             .emplace<transform>(-1.f, 0.f, 0.f)
             .emplace<mount_socket>();
        });
    

    return 0;
}
