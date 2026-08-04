#pragma once
#include <entt/entity/registry.hpp>
#include <unordered_set>

#include "hierarchy.hpp"
#include "persistent_id.hpp"

namespace enttx {

// -----------------------------------------------------------------------
// Concepts
//
// Prefabs:
// - 
//  
// Node:
// - A node is an entity in a prefab hierarchy with a stable identity.
// - A node id is used to reference a node across prefab levels, and is stable across edits. 
//
// -----------------------------------------------------------------------

/*! @brief Type of identifier used to name prefab assets. */
using prefab_id = entt::id_type;

/*! 
 * @brief Type of identifier used to reference persistent entities across a prefab hierarchy. 
 *
 * 
 */
using node_id = persistent_id;

template<typename Registry>
class basic_entity_remap {
    using traits_type = entt::entt_traits<typename Registry::entity_type>;
public:
    /*! @brief Type of registry accepted by the handle. */
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;
    /*! @brief Underlying version type. */
    using version_type = typename traits_type::version_type;

    [[nodiscard]] 
    entity_type translate(entity_type old) const noexcept {
        if (old == entt::null) return entt::null;
        auto it = entt_map.find(old);
        return (it != entt_map.end()) ? it->second : entt::null;
    }

    std::unordered_map<entity_type, entity_type> entt_map;
};

template<typename T>
struct remap_traits {
    template<typename Registry>
    static void apply( Registry& reg, typename Registry::entity_type e, const basic_entity_remap<Registry>& remap ) {
        if constexpr ( requires { T::remap( reg, e, remap ); } ) {
            T::remap(reg, e, remap);
        } else {
            // Default: component holds no entity references, nothing to do.
        }
    }
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
    /*! @brief Underlying version type. */
    using version_type = typename traits_type::version_type;

    using copy_fn   = void (*)(const registry_type&, entity_type, registry_type&, entity_type);
    using remove_fn = void (*)(registry_type&, entity_type);
    using has_fn    = bool (*)(const registry_type&, entity_type);
    using remap_fn  = void (*)(registry_type&, entity_type, const basic_entity_remap<registry_type>&);

    copy_fn   copy   { nullptr };
    remove_fn remove { nullptr };
    has_fn    has    { nullptr };
    remap_fn  remap  { nullptr };
};

template<typename T>
struct component_ops_traits {
    /*! @brief */
    template<typename Registry>
    static constexpr basic_component_ops<Registry> make() {
        using registry_type = Registry;
        using component_ops = basic_component_ops<registry_type>;
        using entity_type   = typename component_ops::entity_type;
        using entity_remap  = basic_entity_remap<registry_type>;

        component_ops ops;

        ops.copy = +[](const registry_type& src, entity_type se, registry_type& dst, entity_type de) {
            dst.template emplace_or_replace<T>(de, src.template get<T>(se));
        };

        ops.remove = +[](registry_type& reg, entity_type e) {
            reg.template remove<T>(e);
        };

        ops.has = +[](const registry_type& reg, entity_type e) -> bool {
            return reg.template all_of<T>(e);
        };

        ops.remap = +[](registry_type& reg, entity_type e, const entity_remap& remap) {
            if (reg.template all_of<T>(e)) {
                remap_traits<T>::apply(reg, e, remap);
            }
        };

        return ops;
    }
};

// -----------------------------------------------------------------------
// Authoring
// -----------------------------------------------------------------------

template<typename Registry>
using basic_prefab_hierarchy = basic_hierarchy<Registry, struct _prefab_hierarchy_tag>;

struct removed_components {
    std::unordered_set<entt::id_type> types;
};

struct nested_prefab_ref {
    prefab_id prefab;
};

// -----------------------------------------------------------------------
// Runtime
// -----------------------------------------------------------------------

struct prefab_instance_root {
    prefab_id source = entt::null;
};

template<typename Registry>
class basic_prefab_registry
{
    using traits_type = entt::entt_traits<typename Registry::entity_type>;
public:
    /*! @brief Type of registry accepted by the handle. */
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;
    /*! @brief Underlying version type. */
    using version_type = typename traits_type::version_type;
    /*! @brief Type of prefab hierarchy. */
	using prefab_hierarchy = basic_prefab_hierarchy<registry_type>;
    /*! @brief Type of node hierarchy. Represents the actual entity hierarchy. */
    using node_hierarchy = basic_hierarchy<registry_type>;

    /*! @brief Underlying registry to store prefab definitions in. */
    registry_type def_reg{};

    // ------------------------------------------------------------- Authoring

    node_id create_prefab(const prefab_id id, const prefab_id base = entt::null) {
        const auto [node, e] = create_node();
        
        auto& hier = def_reg.template emplace<prefab_hierarchy>(e);
        if (const entity_type base_root = get_root_node_entity(base); 
            base_root != entt::null && def_reg.valid(base_root)) {
            prefab_hierarchy::attach_child(def_reg, base_root, e);
        }

        return node;
    }

    node_id add_child(const prefab_id prefab, const node_id parent) {
        const auto [node, e] = create_node();

        const entity_type parent_entity = get_node_entity(prefab, parent);
        node_hierarchy::attach_child(def_reg, parent_entity, e);

        return node;
    }

    node_id add_nested(const prefab_id prefab, const node_id parent, const prefab_id nested_prefab) {
        const auto [node, e] = create_node();

        const entity_type parent_entity = get_node_entity(prefab, parent);
        node_hierarchy::attach_child(def_reg, parent_entity, e);
        emplace<nested_prefab_ref>(prefab, node, nested_prefab_ref{nested_prefab});

        return node;
    }

    template<typename T, typename... Args>
    T& emplace(const prefab_id prefab, const node_id node, Args&&... args) {
        const entity_type e = get_node_entity(prefab, node);
        // TODO: entt handles the null case for us, but we should probably assert here that the node exists?
        return def_reg.template emplace<T>(e, std::forward<Args>(args)...);
    }

    template<typename T>
    void remove(const prefab_id prefab, const node_id node) {
    }

    void remove_child(const prefab_id prefab, const node_id node) {
    }

    // ------------------------------------------------------------- Instantiate

    entity_type instantiate(const prefab_id prefab, registry_type& target) {

    }

    // ------------------------------------------------------------- Introspection

    [[nodiscard]]
    bool is_a(const prefab_id derived, const prefab_id base) const {
        const entity_type base_entity = get_root_node_entity(base);
        entity_type cur = get_root_node_entity(derived);
        while (cur != entt::null && def_reg.valid(cur)) {
            if (cur == base_entity) 
                return true;
            cur = def_reg.template get<prefab_hierarchy>(cur).parent;
        }
        return false;
    }

protected:

    [[nodiscard]]
    std::pair<node_id, entity_type> create_node() {
        const node_id id = generate_persistent_id();
        const entity_type e = def_reg.create();
        def_reg.template emplace<node_id>(e, id);
        return {id, e};
    }

    [[nodiscard]]
    entity_type get_node_entity(const prefab_id prefab, const node_id node) {
        return entt::null;
    }

    [[nodiscard]]
    entity_type get_root_node_entity(const prefab_id prefab) {
        return entt::null;
    }

};

template<typename PrefabRegistry>
class basic_prefab_builder {
public:
    /*! @brief  */
    using prefab_registry_type = PrefabRegistry;

    prefab_registry_type& reg_;
    const prefab_id       prefab_;
    const node_id         node_;

    basic_prefab_builder(prefab_registry_type& reg, const prefab_id prefab, const node_id node)
        : reg_{reg}, prefab_{prefab}, node_{node} {}

    basic_prefab_builder(const basic_prefab_builder&) = delete;
    basic_prefab_builder& operator=(const basic_prefab_builder&) = delete;

    template<typename BuilderFn>
    requires(std::invocable<BuilderFn, basic_prefab_builder&>)
    basic_prefab_builder& add_child(BuilderFn&& fn) {
        const node_id new_child = reg_.add_child(prefab_, node_);
        basic_prefab_builder child_builder{reg_, prefab_, new_child};
        std::invoke(std::forward<BuilderFn>(fn), child_builder);
        return *this;
    }

    basic_prefab_builder& remove_child(node_id child) {
        reg_.remove_child(prefab_, child);
        return *this;
    }

    basic_prefab_builder& add_nested(prefab_id nested_prefab) {
        reg_.add_nested(prefab_, node_, nested_prefab);
        return *this;
    }

    template<typename T, typename... Args>
    basic_prefab_builder& emplace(Args&&... args) {
        reg_.emplace<T>(prefab_, node_, std::forward<Args>(args)...);
        return *this;
    }

    template<typename T>
    basic_prefab_builder& remove() {
        reg_.remove<T>(prefab_, node_);
        return *this;
    }
};

using entity_remap     = basic_entity_remap<entt::registry>;
using component_ops    = basic_component_ops<entt::registry>;
using prefab_hierarchy = basic_prefab_hierarchy<entt::registry>;
using prefab_registry  = basic_prefab_registry<entt::registry>; 
using prefab_builder   = basic_prefab_builder<prefab_registry>;

} // namespace enttx