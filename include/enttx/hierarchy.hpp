#pragma once
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/signal/sigh.hpp>
#include <cstdint>
#include <concepts>
#include <functional>

#include "config.hpp"
#include "entity_remap.hpp"

namespace enttx {

/*! @brief Describes how a hierarchy should handle it's relationship on destruction. */
enum class hierarchy_deletion_policy : std::uint8_t {
    /*! @brief Destroy the children when the parent is destroyed. */
    destroy_children,
    /*! @brief Orphan the children when the parent is destroyed. */
    orphan_children,
    /*! @brief Relationships are not handled on destruction. */
    unhandled
};

/*! @brief Configuration for a basic_hierarchy type. */
struct hierarchy_config {
    /*! @brief Deletion policy for handling hierarchy relationships on destruction. */
    hierarchy_deletion_policy deletion_policy{hierarchy_deletion_policy::destroy_children};
    /*! @brief Whether to enable events for hierarchy changes. Set to false to avoid the overhead of event publishing if you don't need it. */
    bool enable_events{true};
};

namespace internal {

/*! @brief Forward iterator over children, following the `Next` sibling pointer. */
template<typename Hierarchy, typename Hierarchy::entity_type Hierarchy::* Next>
class basic_child_iterator {
public:
    using registry_type     = typename Hierarchy::registry_type;
    using entity_type       = typename Hierarchy::entity_type;
    using iterator_category = std::forward_iterator_tag;
    using value_type        = entity_type;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const entity_type*;
    using reference          = const entity_type&;

    constexpr basic_child_iterator() noexcept = default;
    basic_child_iterator(const registry_type& reg, const entity_type curr) noexcept
        : reg_{&reg}, curr_{curr} {
        if (curr_ != entt::null) {
            const auto* h = reg_->template try_get<Hierarchy>(curr_);
            ENTTX_ASSERT(h != nullptr, "Corrupted hierarchy: child does not have a hierarchy component");
            next_ = h->*Next;
        }
    }

    [[nodiscard]] reference operator*()  const noexcept { return curr_; }
    [[nodiscard]] pointer   operator->() const noexcept { return &curr_; }

    basic_child_iterator& operator++() noexcept {
        curr_ = next_;
        if (curr_ != entt::null) {
            const auto* h = reg_->template try_get<Hierarchy>(curr_);
            ENTTX_ASSERT(h != nullptr, "Corrupted hierarchy: child does not have a hierarchy component");
            next_ = h->*Next;
        }
        return *this;
    }

    basic_child_iterator operator++(int) noexcept {
        basic_child_iterator tmp{*this};
        ++(*this);
        return tmp;
    }

    [[nodiscard]] friend bool operator==(const basic_child_iterator& lhs, const basic_child_iterator& rhs) noexcept {
        return lhs.curr_ == rhs.curr_;
    }

private:
    const registry_type* reg_{nullptr};
    entity_type curr_{entt::null};
    entity_type next_{entt::null};
};

template<typename Hierarchy>
struct basic_children_view {
    using registry_type    = typename Hierarchy::registry_type;
    using entity_type      = typename Hierarchy::entity_type;
    using iterator         = basic_child_iterator<Hierarchy, &Hierarchy::next_sibling>;
    using reverse_iterator = basic_child_iterator<Hierarchy, &Hierarchy::prev_sibling>;

    const registry_type& reg;
    entity_type parent;

    [[nodiscard]] iterator begin() const noexcept {
        const auto* h = reg.template try_get<Hierarchy>(parent);
        return iterator{reg, h ? h->first_child : entt::null};
    }

    [[nodiscard]] iterator end() const noexcept {
        return iterator{};
    }

    [[nodiscard]] reverse_iterator rbegin() const noexcept {
        const auto* h = reg.template try_get<Hierarchy>(parent);
        return reverse_iterator{reg, h ? h->last_child : entt::null};
    }

    [[nodiscard]] reverse_iterator rend() const noexcept {
        return reverse_iterator{};
    }
};

} // namespace internal

/**
 * @brief Double-linked list node for representing a hierarchy of entities.
 * 
 * Represents a node in a double-linked list that can be used to represent a hierarchy of entities.
 * Hierarchy operations maintain a O(1) time complexity like a double-linked list,
 * but without heap allocations or pointer chasing, 
 * while also allowing for cache-friendly iteration over the children of a parent entity.
 * 
 * @tparam Registry Basic registry type.
 * @tparam Config Deletion policy and event-enablement configuration for the hierarchy.
 * @tparam _ Tag to distinguish different hierarchies in the same registry.
 */
template<
    typename Registry, 
    hierarchy_config Config = hierarchy_config{},
    typename = void>
struct basic_hierarchy {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
    /*! @brief Type of registry */ 
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;
    /*! @brief Underlying version type. */
    using version_type = typename traits_type::version_type;
    /*! @brief Unsigned integer type. */
    using size_type = std::uint32_t;
    /*! @brief Configuration for the hierarchy. */
    static constexpr hierarchy_config config{ Config };
    /*! @brief Deletion policy for handling hierarchy relationships on destruction. */
    static constexpr hierarchy_deletion_policy deletion_policy{ Config.deletion_policy };

    /*! @brief Signals published for hierarchy mutations. Only present in the registry's
     *         context (via ensure_events/on_* below) if events are actually connected to. */
    struct hierarchy_events {
        entt::sigh<void(registry_type&, entity_type /*parent*/, entity_type /*child*/)> child_added{};
        entt::sigh<void(registry_type&, entity_type /*parent*/, entity_type /*child*/)> child_removed{};
        entt::sigh<void(registry_type&, entity_type /*child*/, entity_type /*old_parent*/, entity_type /*new_parent*/)> reparented{};
        entt::sigh<void(registry_type&, entity_type /*entity*/)> before_destroy{};
    };

    entity_type parent       {entt::null};
    entity_type first_child  {entt::null};
    entity_type last_child   {entt::null};
    entity_type next_sibling {entt::null};
    entity_type prev_sibling {entt::null};
    size_type   child_count  {0u};

    /*! @brief Forward iterator over the direct children of a parent entity. */
    using child_iterator         = internal::basic_child_iterator<basic_hierarchy, &basic_hierarchy::next_sibling>;
    /*! @brief Reverse iterator over the direct children of a parent entity. */
    using reverse_child_iterator = internal::basic_child_iterator<basic_hierarchy, &basic_hierarchy::prev_sibling>;
    /*! @brief Lightweight range over a parent's direct children. */
    using children_view          = internal::basic_children_view<basic_hierarchy>;

    /*! @brief Checks if a parent entity has any direct children. */
    [[nodiscard]]
    bool has_children() const noexcept {
        return child_count > 0u;
    }

    /*!
     * @brief Returns a range over the direct children of a parent for use with range-for
     *        or standard algorithms.
     * @param reg The registry containing the entities.
     * @param parent The parent entity whose children will be iterated.
	 * @warning It is safe to destroy the current entity being iterated, 
     *          but it is not safe to destroy the parent entity or any other sibling entities during iteration.
     */
    [[nodiscard]]
    static children_view children( const registry_type& reg, const entity_type parent ) noexcept {
        return children_view{ reg, parent };
    }

    /*!
     * @brief Detaches a child entity from its parent and siblings.
     * @param reg The registry containing the entities.
     * @param child The child entity to detach.
     */
    static void detach(registry_type& reg, const entity_type child) {
        detach_impl(reg, child, /*notify=*/true);
    }

    /*!
     * @brief Removes all direct children from a parent entity.
     * @param reg The registry to operate on.
     * @param parent The parent entity whose children to orphan.
     */
    static void orphan_children(registry_type& reg, const entity_type parent) {
        auto* ph = reg.template try_get<basic_hierarchy>(parent);
        if (!ph) {
            return;
        }

        entity_type child = ph->first_child;
        while (child != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(child), "Corrupted hierarchy: child does not have a basic_hierarchy component");
            auto& ch = reg.template get<basic_hierarchy>(child);
            const entity_type next_child = ch.next_sibling;
            ch.parent = ch.next_sibling = ch.prev_sibling = entt::null;
            child = next_child;
        }

        ph->first_child = ph->last_child = entt::null;
        ph->child_count = 0u;
    }

    /*!
     * @brief Inserts a child entity before another child entity in the parent's list of children.
     * @param reg The registry to operate on.
     * @param before The child entity before which to insert.
     * @param child The child entity to insert.
     */
    static void insert_before(registry_type& reg, const entity_type before, const entity_type child) {
        if (child == entt::null || before == entt::null || before == child) {
            return;
        }

        ENTTX_USER_ASSERT(reg.valid(child), "Child entity is not valid");
        ENTTX_USER_ASSERT(reg.template all_of<basic_hierarchy>(before), "Before entity does not have a basic_hierarchy component");

        const entity_type parent = reg.template get<basic_hierarchy>(before).parent;
        if (parent == entt::null) {
            return;
        }

        const entity_type old_parent = begin_move(reg, child);
        ENTTX_USER_ASSERT(!is_descendant(reg, parent, child), "Cannot insert a parent as a child of its descendant");

        // Read after detach: `before` may have been child's old neighbor.
        const entity_type prev = reg.template get<basic_hierarchy>(before).prev_sibling;
        auto& ph = reg.template get<basic_hierarchy>(parent);
        auto& ch = reg.template get_or_emplace<basic_hierarchy>(child);
        link_child(reg, ph, ch, parent, child, prev, before);
        notify_attach(reg, old_parent, parent, child);
    }

    /*!
     * @brief Inserts a child entity after another child entity in the parent's list of children.
     * @param reg The registry to operate on.
     * @param after The child entity after which to insert.
     * @param child The child entity to insert.
     */
    static void insert_after(registry_type& reg, const entity_type after, const entity_type child) {
        if (child == entt::null || after == entt::null || after == child) {
            return;
        }

        ENTTX_USER_ASSERT(reg.valid(child), "Child entity is not valid");
        ENTTX_USER_ASSERT(reg.template all_of<basic_hierarchy>(after), "After entity does not have a basic_hierarchy component");

        const entity_type parent = reg.template get<basic_hierarchy>(after).parent;
        if (parent == entt::null) {
            return;
        }

        const entity_type old_parent = begin_move(reg, child);
        ENTTX_USER_ASSERT(!is_descendant(reg, parent, child), "Cannot insert a parent as a child of its descendant");

        // Read after detach: `after` may have been child's old neighbor.
        const entity_type next = reg.template get<basic_hierarchy>(after).next_sibling;
        auto& ph = reg.template get<basic_hierarchy>(parent);
        auto& ch = reg.template get_or_emplace<basic_hierarchy>(child);
        link_child(reg, ph, ch, parent, child, after, next);
        notify_attach(reg, old_parent, parent, child);
    }

    /*!
     * @brief Adds a child entity to the front of the parent's list of children.
     * @param reg The registry to operate on.
     * @param parent The parent entity.
     * @param child The child entity to add.
     */
    static void push_front(registry_type& reg, const entity_type parent, const entity_type child) {
        if (parent == entt::null || child == entt::null || parent == child) {
            return;
        }

        ENTTX_USER_ASSERT(reg.valid(parent), "Parent entity is not valid");
        ENTTX_USER_ASSERT(reg.valid(child), "Child entity is not valid");

        const entity_type old_parent = begin_move(reg, child);
        ENTTX_USER_ASSERT(!is_descendant(reg, parent, child), "Cannot push a parent as a child of its descendant");

        auto& ph = reg.template get_or_emplace<basic_hierarchy>(parent);
        auto& ch = reg.template get_or_emplace<basic_hierarchy>(child);
        link_child(reg, ph, ch, parent, child, entt::null, ph.first_child);
        notify_attach(reg, old_parent, parent, child);
    }

    /*!
     * @brief Adds a child entity to the back of the parent's list of children.
     * @param reg The registry to operate on.
     * @param parent The parent entity.
     * @param child The child entity to add.
     */
    static void push_back(registry_type& reg, const entity_type parent, const entity_type child) {
        if (parent == entt::null || child == entt::null || parent == child) {
            return;
        }

        ENTTX_USER_ASSERT(reg.valid(parent), "Parent entity is not valid");
        ENTTX_USER_ASSERT(reg.valid(child), "Child entity is not valid");

        const entity_type old_parent = begin_move(reg, child);
        ENTTX_USER_ASSERT(!is_descendant(reg, parent, child), "Cannot push a parent as a child of its descendant");

        auto& ph = reg.template get_or_emplace<basic_hierarchy>(parent);
        auto& ch = reg.template get_or_emplace<basic_hierarchy>(child);
        link_child(reg, ph, ch, parent, child, ph.last_child, entt::null);
        notify_attach(reg, old_parent, parent, child);
    }

    /*!
     * @brief Visits direct children of a parent entity in sibling order.
     * @tparam Fn A callable type that takes an entity_type as an argument.
     * @param reg The registry containing the entities.
     * @param parent The parent entity whose children will be visited.
     * @param fn The callable to invoke for each child entity.
     */
    template<typename Fn>
    requires std::invocable<Fn&, entity_type>
    static void for_each_child(const registry_type& reg, const entity_type parent, Fn&& fn) {
        for (const entity_type child : children(reg, parent)) {
            std::invoke(fn, child);
        }
    }

    /*!
     * @brief Visits every descendant of a parent entity in depth-first post-order (children before parent, siblings in order).
     * @tparam Fn A callable type that takes an entity_type as an argument.
     * @param reg The registry containing the entities.
     * @param parent The parent entity whose descendants will be visited.
     * @param fn The callable to invoke for each descendant entity.
     */
    template<typename Fn>
    requires std::invocable<Fn&, entity_type>
    static void for_each_descendant(const registry_type& reg, const entity_type parent, Fn&& fn) {
        for_each_child(reg, parent, [&](const entity_type child) {
            for_each_descendant( reg, child, fn );
            std::invoke(fn, child);
        });
    }

    /*!
     * @brief Finds the root ancestor of a given entity.
     * @param reg The registry containing the entities.
     * @param e The entity whose root ancestor will be found.
     * @return The root ancestor entity
     */
    [[nodiscard]]
    static entity_type find_root(const registry_type& reg, entity_type e) {
        entity_type root = e;
        while (const auto* h = reg.template try_get<basic_hierarchy>(e)) {
            const entity_type p = h->parent;
            if (p == entt::null || !reg.template all_of<basic_hierarchy>(p))
                break;

            ENTTX_ASSERT(p != e, "Corrupted hierarchy: Cycle detected in hierarchy");
            root = e = p;
        }
        return root;
    }

    /*!
     * @brief Checks if an entity is a descendant of another entity.
     * @param reg The registry containing the entities.
     * @param e The entity to check.
     * @param ancestor The potential ancestor entity.
     * @return True if `e` is a descendant of `ancestor`, false otherwise.
     */
    [[nodiscard]]
    static bool is_descendant(const registry_type& reg, entity_type e, const entity_type ancestor) {
        while (e != entt::null) {
            const auto* h = reg.template try_get<basic_hierarchy>(e);
            if (!h)
                break;

            e = h->parent;
            if (e == ancestor)
                return true;
        }

        return false;
    }

    // ------------------------------------------------------------- Events

    [[nodiscard]] static auto on_child_added(registry_type& reg)
    requires (config.enable_events) {
        return entt::sink{ ensure_events(reg).child_added };
    }
    [[nodiscard]] static auto on_child_removed(registry_type& reg)
    requires (config.enable_events) {
        return entt::sink{ ensure_events(reg).child_removed };
    }
    [[nodiscard]] static auto on_reparented(registry_type& reg)
    requires (config.enable_events) {
        return entt::sink{ ensure_events(reg).reparented };
    }
    [[nodiscard]] static auto on_before_destroy(registry_type& reg)
    requires (config.enable_events) {
        return entt::sink{ ensure_events(reg).before_destroy };
    }

    // ------------------------------------------------------------- 

    /*! @brief Implements the remap_traits (see `entity_remap.hpp`). */
    static void remap(
        registry_type& reg,
        const entity_type e,
        const is_entity_remapper<registry_type> auto& remapper )
    {
        if ( auto* h = reg.template try_get<basic_hierarchy>( e ) )
        {
            h->parent = remapper( h->parent );
            h->first_child = remapper( h->first_child );
            h->last_child = remapper( h->last_child );
            h->next_sibling = remapper( h->next_sibling );
            h->prev_sibling = remapper( h->prev_sibling );
        }
    }

    /*!
     * @brief Handles the destruction of an entity in the hierarchy.
     * @param reg The registry to operate on.
     * @param e The entity being destroyed.
     * @note This takes advantage of the auto-connection of the `on_destroy` signal in EnTT. 
     */
    static void on_destroy(registry_type& reg, const entity_type e) 
    requires (deletion_policy != hierarchy_deletion_policy::unhandled) {
        if (!reg.template all_of<basic_hierarchy>(e)) {
            return;
        }

        if constexpr (config.enable_events) {
            if (auto* ev = reg.ctx().template find<hierarchy_events>()) {
                ev->before_destroy.publish(reg, e);
            }
        }

        detach(reg, e);

        if constexpr (deletion_policy == hierarchy_deletion_policy::destroy_children) {
            entity_type child = reg.template get<basic_hierarchy>(e).first_child;
            while (child != entt::null) {
                ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(child), "Corrupted hierarchy: child does not have a basic_hierarchy component");
                const entity_type next = reg.template get<basic_hierarchy>(child).next_sibling;
                reg.template get<basic_hierarchy>(child).parent = entt::null; // skip patching e, it's dying anyway
                reg.destroy(child);
                child = next;
            }
        } else if constexpr (deletion_policy == hierarchy_deletion_policy::orphan_children) {
            orphan_children(reg, e);
        }
    }

protected:

    /*!
     * @brief Detaches a child entity from its parent and siblings.
     * @param reg The registry containing the entities.
     * @param child The child entity to detach.
     * @param notify Whether to publish `child_removed`. Callers that are about to
     *        re-attach `child` elsewhere in the same operation (push_front, push_back,
     *        insert_before, insert_after) pass false and fire `reparented`/`child_added`
     *        themselves once the new links are in place, so a move never shows up as a
     *        spurious remove+add pair.
     */
    static void detach_impl(registry_type& reg, const entity_type child, bool notify) {
        auto* h = reg.template try_get<basic_hierarchy>(child);
        if (!h) {
            return;
        }

        const entity_type old_parent = h->parent;

        // Patch the previous sibling's next_sibling to skip over `child`
        if (h->prev_sibling != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(h->prev_sibling), "Corrupted hierarchy: previous sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(h->prev_sibling).next_sibling = h->next_sibling;
        }

        // Patch the next sibling's prev_sibling to skip over `child`
        if (h->next_sibling != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(h->next_sibling), "Corrupted hierarchy: next sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(h->next_sibling).prev_sibling = h->prev_sibling;
        }

        // If `child` was the first/last child of its parent, update the parent's links
        if (h->parent != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(h->parent), "Corrupted hierarchy: parent does not have a basic_hierarchy component");
            auto& ph = reg.template get<basic_hierarchy>(h->parent);
            if (ph.first_child == child) ph.first_child = h->next_sibling;
            if (ph.last_child == child)  ph.last_child  = h->prev_sibling;

            ENTTX_ASSERT(ph.child_count > 0, "Parent's children count should be greater than zero");
            ph.child_count--;
        }

        // Reset the child's hierarchy links
        h->parent = h->next_sibling = h->prev_sibling = entt::null;

        if constexpr (config.enable_events) {
            if (notify && old_parent != entt::null) {
                if (auto* ev = reg.ctx().template find<hierarchy_events>()) {
                    ev->child_removed.publish(reg, old_parent, child);
                }
            }
        }
    }

    /*!
     * @brief Finds or creates this hierarchy's event hub in the registry's context.
     * @note Uses find-then-emplace rather than a bare `ctx().emplace<T>()` because
     *       re-emplacing an existing context variable in EnTT reconstructs it in place,
     *       which would silently drop connections made by an earlier `on_*` call.
     */
    [[nodiscard]] static hierarchy_events& ensure_events(registry_type& reg)
    requires (config.enable_events) {
        if (auto* ev = reg.ctx().template find<hierarchy_events>()) {
            return *ev;
        }
        return reg.ctx().template emplace<hierarchy_events>();
    }

    /*!
     * @brief Publishes the correct attach-side event (`child_added` or `reparented`)
     *        after a child's links have been updated to point at `new_parent`.
     * @note Fires nothing when `old_parent == new_parent`, i.e. a pure reorder via
     *       insert_before/insert_after within the same parent. Add a `child_reordered`
     *       signal later if that distinction becomes useful.
     */
    static void notify_attach(registry_type& reg, const entity_type old_parent,
                              const entity_type new_parent, const entity_type child) {
        if constexpr (config.enable_events) {
            if (auto* ev = reg.ctx().template find<hierarchy_events>()) {
                if (old_parent == entt::null) {
                    ev->child_added.publish(reg, new_parent, child);
                } else if (old_parent != new_parent) {
                    ev->reparented.publish(reg, child, old_parent, new_parent);
                }
            }
        }
    }

    /*! @brief Detaches `child` without notifying and returns its previous parent. */
    static entity_type begin_move(registry_type& reg, const entity_type child) {
        const auto* h = reg.template try_get<basic_hierarchy>(child);
        const entity_type old_parent = h ? h->parent : entt::null;
        detach_impl(reg, child, /*notify=*/false);
        return old_parent;
    }

    /*! @brief Links `child` into `parent`'s list between `prev` and `next` (either may be null for front/back). */
    static void link_child(registry_type& reg, basic_hierarchy& ph, basic_hierarchy& ch,
                           const entity_type parent, const entity_type child,
                           const entity_type prev, const entity_type next) {
        ch.parent       = parent;
        ch.prev_sibling = prev;
        ch.next_sibling = next;

        if (prev != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(prev), "Corrupted hierarchy: previous sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(prev).next_sibling = child;
        } else {
            ph.first_child = child;
        }

        if (next != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(next), "Corrupted hierarchy: next sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(next).prev_sibling = child;
        } else {
            ph.last_child = child;
        }

        ph.child_count++;
    }
};

/*! @brief Alias declaration for the most common use case. */
using hierarchy = basic_hierarchy<entt::registry, 
                                  hierarchy_config{ .deletion_policy = hierarchy_deletion_policy::destroy_children, .enable_events = true },
                                  struct default_hierarchy_tag>;

} // namespace enttx
