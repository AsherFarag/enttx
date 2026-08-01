#pragma once
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>

namespace enttx {

    // Features:
    // Delta serialization only
    // IsA relationships

    using prefab_id = entt::id_type;

    template<typename Registry>
    struct basic_component_ops {
        using remove_fn = void(*)();
    };

    template<typename Registry>
    struct basic_prefab_node {
        prefab_id id = entt::null;
        prefab_node parent = entt::null;
    };

    template<typename Registry>
    class basic_prefab_registry {
    };

    using component_ops = basic_component_ops<entt::registry>;
    using prefab_node = basic_prefab_node<entt::registry>;
    using prefab_registry = basic_prefab_registry<entt::registry>;

} // namespace enttx