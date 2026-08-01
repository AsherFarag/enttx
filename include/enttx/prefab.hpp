#pragma once
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>

namespace enttx {

// Features:
// Delta serialization only
// IsA relationships

using prefab_id = entt::id_type;

struct prefab_tag {};

struct prefab_instance_tag {
    prefab_id prefab = entt::null;
};

template<typename Registry>
struct basic_component_ops {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
    /*! @brief Type of registry accepted by the handle. */
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;

    using copy_fn = void(*)(const registry_type& src_reg, 
                            const entity_type src, 
                            registry_type& dst_reg, 
                            const entity_type dst);

    copy_fn copy = nullptr;
};

template<typename Registry>
struct basic_prefab_node {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
    /*! @brief Type of registry accepted by the handle. */
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;

    prefab_id id = entt::null;
    prefab_node parent = entt::null;
};

template<typename Registry>
class basic_prefab_registry {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
    /*! @brief Type of registry accepted by the handle. */
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;

    registry_type reg;
};

using component_ops = basic_component_ops<entt::registry>;
using prefab_node = basic_prefab_node<entt::registry>;
using prefab_registry = basic_prefab_registry<entt::registry>;

} // namespace enttx