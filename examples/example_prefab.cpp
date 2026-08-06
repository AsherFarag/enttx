// prefab_example.cpp
//
// A small tour of the `enttx` prefab library using only the public API.
// This example shows how a typical user would create prefab definitions,
// customize them, and create runtime instances.
//
// Covers:
//   1. Creating components and a prefab_registry
//   2. Creating a prefab with components and child objects
//   3. Instantiating prefabs into a world registry
//   4. Creating derived prefabs and overriding inherited data
//   5. Modifying inherited child nodes
//   6. Adding new children to derived prefabs
//   7. Using nested prefabs
//   8. Querying prefab relationships
//   9. Detaching instances from prefab tracking
//
// This file is intended as a simple introduction to the library.
// It assumes prefab.hpp and hierarchy.hpp are available on the include path
// and that EnTT is installed.

#include <entt/entt.hpp>
#include <format>
#include <iostream>
#include <string>

#include <enttx/prefab.hpp>

// -----------------------------------------------------------------------
// Components
// -----------------------------------------------------------------------
// Components are just normal EnTT components.
// No special setup is required.

struct transform {
    float x{0.0f};
    float y{0.0f};
};

struct name {
    std::string value;
};

struct health {
    int current{0};
    int max{0};
};

struct sprite {
    std::string texture;
    int layer{0};
};

int main() {
	using namespace entt::literals; // for _hs literal

    // Create a prefab definition registry and a world registry.
    entt::registry def_reg;
    entt::registry world;

    enttx::prefab_registry prefabs{def_reg};

    // -------------------------------------------------------------------
    // Create a base prefab
    // -------------------------------------------------------------------

    const enttx::prefab_id goblin_id = "goblin"_hs;
    const enttx::node_id goblin_root = prefabs.create_prefab(goblin_id);

    prefabs.emplace<name>(goblin_id, goblin_root, name{"Goblin"});
    prefabs.emplace<transform>(goblin_id, goblin_root, transform{0.0f, 0.0f});
    prefabs.emplace<health>(goblin_id, goblin_root, health{10, 10});
    prefabs.emplace<sprite>(goblin_id, goblin_root, sprite{"goblin.png", 0});

    // Add a child object to the prefab.
    const enttx::node_id goblin_weapon = prefabs.add_child(goblin_id, goblin_root);

    prefabs.emplace<name>(goblin_id, goblin_weapon, name{"Rusty Dagger"});
    prefabs.emplace<sprite>(goblin_id, goblin_weapon, sprite{"dagger.png", 1});

    // -------------------------------------------------------------------
    // Create an instance
    // -------------------------------------------------------------------

    const entt::entity goblin_instance = prefabs.instantiate(goblin_id, world);

    std::cout << std::format(
        "Instantiated goblin: {} (hp {}/{})\n",
        world.get<name>(goblin_instance).value,
        world.get<health>(goblin_instance).current,
        world.get<health>(goblin_instance).max
    );

    // -------------------------------------------------------------------
    // Create a derived prefab
    // -------------------------------------------------------------------

    // Derived prefabs inherit from their parent and can override values.
    const enttx::prefab_id chief_id = "goblin_chief"_hs;
    const enttx::node_id chief_root = prefabs.create_prefab(chief_id, goblin_id);

    prefabs.emplace<name>(chief_id, chief_root, name{"Goblin Chief"});
    prefabs.emplace<health>(chief_id, chief_root, health{40, 40});

    // Remove an inherited component.
    prefabs.remove<sprite>(chief_id, chief_root);

    // -------------------------------------------------------------------
    // Override inherited children
    // -------------------------------------------------------------------

    // Modify an existing inherited child.
    prefabs.override_child(chief_id, goblin_weapon);

    prefabs.emplace<name>(chief_id, goblin_weapon, name{"Chief's Cleaver"});
    prefabs.emplace<sprite>(chief_id, goblin_weapon, sprite{"cleaver.png", 1});

    // Add a new child that only exists on this prefab.
    const enttx::node_id chief_banner = prefabs.add_child(chief_id, chief_root);

    prefabs.emplace<name>(chief_id, chief_banner, name{"War Banner"});
    prefabs.emplace<sprite>(chief_id, chief_banner, sprite{"banner.png", 2});

    // -------------------------------------------------------------------
    // Nested prefabs
    // -------------------------------------------------------------------

    const enttx::prefab_id campfire_id = "campfire"_hs;
    const enttx::node_id campfire_root = prefabs.create_prefab(campfire_id);

    prefabs.emplace<name>(campfire_id, campfire_root, name{"Campfire"});
    prefabs.emplace<sprite>(campfire_id, campfire_root, sprite{"campfire.png", 0});

    // Add the campfire prefab as part of the chief.
    prefabs.add_nested(chief_id, chief_root, campfire_id);

    // Create the final prefab instance.
    const entt::entity chief_instance = prefabs.instantiate(chief_id, world);

    std::cout << std::format(
        "\nInstantiated {} (hp {}/{}, has texture={})\n",
        world.get<name>(chief_instance).value,
        world.get<health>(chief_instance).current,
        world.get<health>(chief_instance).max,
		world.all_of<sprite>( chief_instance ) ? "true" : "false" // sprite was removed from the chief prefab at line 106.
    );

    std::cout << "Children of the chief:\n";

    enttx::prefab_registry::node_hierarchy::for_each_child(world, chief_instance, [&](entt::entity child) {
        std::cout << std::format(
            "  - {}\n",
            world.get<name>(child).value
        );
    });

    // -------------------------------------------------------------------
    // Prefab information
    // -------------------------------------------------------------------

    std::cout << std::format(
        "\nis_a(goblin_chief, goblin)? {}\n",
        prefabs.is_a(chief_id, goblin_id) ? "true" : "false"
    );

    std::cout << std::format(
        "is_a(goblin, goblin_chief)? {}\n",
        prefabs.is_a(goblin_id, chief_id) ? "true" : "false"
    );

    if (const enttx::prefab_id base = prefabs.get_base(chief_id); base != entt::null) {
        std::cout << std::format(
            "goblin_chief's base prefab id: {}\n",
            static_cast<std::uint32_t>(base)
        );
    }

    std::cout << "goblin's direct derived prefabs: ";

    for (const enttx::prefab_id derived : prefabs.derived(goblin_id)) {
        std::cout << std::format(
            "{} ",
            static_cast<std::uint32_t>(derived)
        );
    }

    std::cout << "\n";

    // -------------------------------------------------------------------
    // Detach an instance
    // -------------------------------------------------------------------

    // Removes prefab tracking from this instance.
    prefabs.unpack(world, chief_instance);

    std::cout << std::format(
        "\nchief_instance still has prefab_instance_root? {}\n",
        world.all_of<enttx::prefab_instance_root>(chief_instance) ? "true" : "false"
    );

    return 0;
}