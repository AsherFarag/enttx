#pragma once
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>

namespace enttx {

// TODO: Add last_child to basic_hierarchy for reverse iteration of children.
// Also for linked list style of insertion/removal of children.

/**
 * @brief Intrusive basic_hierarchy component for EnTT.
 *
 * This component is meant to be attached to entities in an EnTT registry to
 * represent a tree-like hierarchy. Each entity can have a parent, a first child,
 * and siblings.
 *
 * @tparam Registry Basic registry type.
 */
template<typename Registry, typename Tag = void>
struct basic_hierarchy {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
    /*! @brief Type of registry accepted by the handle. */
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;
    /*! @brief Underlying version type. */
    using version_type = typename traits_type::version_type;
    /*! @brief Unsigned integer type. */
    using size_type = std::size_t;

    // TODO: Iterator support for both children and descendants.
    // Also add reverse iterators via last_child??
    //struct child_iterator {
    //};

    entity_type parent       = entt::null;
    entity_type first_child  = entt::null;
    entity_type next_sibling = entt::null;
    entity_type prev_sibling = entt::null;

    /**
     * @brief Detaches a child entity from its parent and siblings.
     * @param reg The registry containing the entities.
     * @param child The child entity to detach.
     */
    static void detach(registry_type& reg, const entity_type child) {
        if (!reg.template all_of<basic_hierarchy>(child)) 
            return;

        auto& h = reg.template get<basic_hierarchy>(child);

        // Patch the previous sibling's next_sibling to skip over `child`
        if (h.prev_sibling != entt::null && 
            reg.valid(h.prev_sibling) && 
            reg.template all_of<basic_hierarchy>(h.prev_sibling)) {
            auto& ph = reg.template get<basic_hierarchy>(h.prev_sibling);
            ph.next_sibling = h.next_sibling;
        }
        
        // Patch the next sibling's prev_sibling to skip over `child`
        if (h.next_sibling != entt::null && 
            reg.valid(h.next_sibling) && 
            reg.template all_of<basic_hierarchy>(h.next_sibling)) {
            auto& nh = reg.template get<basic_hierarchy>(h.next_sibling);
            nh.prev_sibling = h.prev_sibling;
        }

        // If `child` was the first child of its parent, update the parent's first_child
        if (h.parent != entt::null && 
            reg.valid(h.parent) && 
            reg.template all_of<basic_hierarchy>(h.parent)) {
            auto& ph = reg.template get<basic_hierarchy>(h.parent);
            if (ph.first_child == child)
                ph.first_child = h.next_sibling;
        }
        
        // Reset the child's hierarchy links
        h.parent = h.next_sibling = h.prev_sibling = entt::null;
    }

    /**
     * @brief Attaches a child entity to a parent entity.
     * @param reg The registry containing the entities.
     * @param parent The parent entity to attach the child to.
     * @param child The child entity to attach.
     * @note This will detach the child from its current parent and siblings if it has any.
     */
    static void attach_child(registry_type& reg, const entity_type parent, const entity_type child) {
        detach(reg, child);

        auto& ph = reg.template get_or_emplace<basic_hierarchy>(parent);
        auto& ch = reg.template get_or_emplace<basic_hierarchy>(child);

        ch.parent       = parent;
        ch.next_sibling = ph.first_child;
        ch.prev_sibling = entt::null;

        if (ph.first_child != entt::null && reg.valid(ph.first_child))
            reg.template get<basic_hierarchy>(ph.first_child).prev_sibling = child;

        ph.first_child = child;
    }

    /**
     * @brief Visits direct children of a parent entity in sibling order.
     * @tparam Fn A callable type that takes an entity_type as an argument.
     * @param reg The registry containing the entities.
     * @param parent The parent entity whose children will be visited.
     * @param fn The callable to invoke for each child entity.
     * @note The callable `fn` can safely detach or destroy the visited child entity.
     */
    template<typename Fn>
    requires std::invocable<Fn&, entity_type>
    static void for_each_child(const registry_type& reg, const entity_type parent, Fn&& fn) {
        if (!reg.template all_of<basic_hierarchy>(parent)) 
            return;
            
        entity_type cur = reg.template get<basic_hierarchy>(parent).first_child;
        while (cur != entt::null && reg.valid(cur)) {
            const entity_type next = reg.template get<basic_hierarchy>(cur).next_sibling;
            std::invoke(std::forward<Fn>(fn), cur);
            cur = next;
        }
    }

    /**
     * @brief Visits every descendant of a parent entity in depth-first pre-order.
     * @tparam Fn A callable type that takes an entity_type as an argument.
     * @param reg The registry containing the entities.
     * @param parent The parent entity whose descendants will be visited.
     * @param fn The callable to invoke for each descendant entity.
     */
    template<typename Fn>
    requires std::invocable<Fn&, entity_type>
    static void for_each_descendant(const registry_type& reg, const entity_type parent, Fn&& fn) {
        for_each_child(reg, parent, [&](const entity_type child) {
            std::invoke(std::forward<Fn>(fn), child);
            for_each_descendant(reg, child, std::forward<Fn>(fn));
        });
    }

    /**
     * @brief Finds the root ancestor of a given entity.
     * @param reg The registry containing the entities.
     * @param e The entity whose root ancestor will be found.
     * @return The root ancestor entity, or entt::null if no valid ancestor exists.
     */
    static entity_type find_root(const registry_type& reg, entity_type e) {
        entity_type root = entt::null;
        while (reg.template all_of<basic_hierarchy>(e)) {
            const entity_type p = reg.template get<basic_hierarchy>(e).parent;
            if (p == entt::null || !reg.valid(p))
                break;

            root = p;
            e = p;
        }
        return root;
    }
};

using hierarchy = basic_hierarchy<entt::registry, struct default_hierarchy_tag>;

} // namespace enttx