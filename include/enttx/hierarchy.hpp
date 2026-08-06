#pragma once
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <cstdint>
#include <concepts>
#include <functional>

#include "config.hpp"

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

/**
 * @brief Double-linked list node for representing a hierarchy of entities.
 * 
 * Represents a node in a double-linked list that can be used to represent a hierarchy of entities.
 * Hierarchy operations maintain a O(1) time complexity like a double-linked list,
 * but without heap allocations or pointer chasing, 
 * while also allowing for cache-friendly iteration over the children of a parent entity.
 * 
 * @tparam Registry Basic registry type.
 * @tparam Policy Deletion policy for handling hierarchy relationships on destruction.
 * @tparam Tag To distinguish different hierarchies in the same registry.
 */
template<
    typename Registry, 
    hierarchy_deletion_policy Policy = hierarchy_deletion_policy::destroy_children, 
    typename Tag = void>
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
    using size_type = std::uint32_t;
    /*! @brief Deletion policy for handling hierarchy relationships on destruction. */
    static constexpr auto deletion_policy{ Policy };

    entity_type parent       {entt::null};
    entity_type first_child  {entt::null};
    entity_type last_child   {entt::null};
    entity_type next_sibling {entt::null};
    entity_type prev_sibling {entt::null};
    size_type   child_count  {0u};

    /*! 
     * @brief Forward iterator over the direct children of a parent entity.
     * @tparam Next Pointer to the member of basic_hierarchy that points to the next sibling.
     */
    template<entity_type basic_hierarchy::* Next>
    class basic_child_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = entity_type;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const entity_type*;
        using reference         = const entity_type&;
    
        constexpr basic_child_iterator() noexcept = default;
        basic_child_iterator(const registry_type& reg, const entity_type curr) noexcept
            : reg_{&reg}, curr_{curr} {
                if (curr_ != entt::null) {
                    ENTTX_ASSERT(reg_->template all_of<basic_hierarchy>(curr_),
                        "Corrupted hierarchy: child does not have a basic_hierarchy component");
                    next_ = reg_->template get<basic_hierarchy>(curr_).*Next;
                }
        }
    
        [[nodiscard]] reference operator*()  const noexcept { return curr_; }
        [[nodiscard]] pointer   operator->() const noexcept { return &curr_; }
    
        basic_child_iterator& operator++() noexcept {
            curr_ = next_;
            if (curr_ != entt::null) {
                ENTTX_ASSERT(reg_->template all_of<basic_hierarchy>(curr_),
                    "Corrupted hierarchy: child does not have a basic_hierarchy component");
                next_ = reg_->template get<basic_hierarchy>(curr_).*Next;
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
    
    /*! @brief Type of forward iterator over the direct children of a parent entity. */
    using child_iterator         = basic_child_iterator<&basic_hierarchy::next_sibling>;
    /*! @brief Type of reverse iterator over the direct children of a parent entity. */
    using reverse_child_iterator = basic_child_iterator<&basic_hierarchy::prev_sibling>;

    /*! @brief Lightweight range over a parent's direct children. */
    struct children_view {
        using iterator         = child_iterator;
        using reverse_iterator = reverse_child_iterator;

        const registry_type& reg;
        entity_type parent;

        [[nodiscard]] iterator begin() const noexcept {
            entity_type first = entt::null;
            if ( reg.template all_of<basic_hierarchy>( parent ) ) {
                first = reg.template get<basic_hierarchy>( parent ).first_child;
            }
			return iterator{reg, first};
        }

        [[nodiscard]] iterator end() const noexcept {
            return iterator{};
        }

        [[nodiscard]] reverse_iterator rbegin() const noexcept {
            entity_type last = entt::null;
            if ( reg.template all_of<basic_hierarchy>( parent ) ) {
                last = reg.template get<basic_hierarchy>( parent ).last_child;
            }
            return reverse_iterator{reg, last};
        }

        [[nodiscard]] reverse_iterator rend() const noexcept {
            return reverse_iterator{};
        }
    };

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
        if (!reg.template all_of<basic_hierarchy>(child)) 
            return;

        auto& h = reg.template get<basic_hierarchy>(child);

        // Patch the previous sibling's next_sibling to skip over `child`
        if (h.prev_sibling != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(h.prev_sibling), "Corrupted hierarchy: previous sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(h.prev_sibling).next_sibling = h.next_sibling;
        }
        
        // Patch the next sibling's prev_sibling to skip over `child`
        if (h.next_sibling != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(h.next_sibling), "Corrupted hierarchy: next sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(h.next_sibling).prev_sibling = h.prev_sibling;
        }

        // If `child` was the first child of its parent, update the parent's first_child
        if (h.parent != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(h.parent), "Corrupted hierarchy: parent does not have a basic_hierarchy component");
            auto& ph = reg.template get<basic_hierarchy>(h.parent);
            if (ph.first_child == child)
                ph.first_child = h.next_sibling;
            if (ph.last_child == child)
                ph.last_child = h.prev_sibling;

			ENTTX_ASSERT(ph.child_count > 0, "Parent's children count should be greater than zero");
            ph.child_count--;
        }
        
        // Reset the child's hierarchy links
        h.parent = h.next_sibling = h.prev_sibling = entt::null;
    }

    /*!
     * @brief Removes all direct children from a parent entity.
     * @param reg The registry to operate on.
     * @param parent The parent entity whose children to orphan.
     */
    static void orphan_children(registry_type& reg, const entity_type parent) {
        if (!reg.template all_of<basic_hierarchy>(parent))
            return;

        auto& ph = reg.template get<basic_hierarchy>(parent);
        entity_type child = ph.first_child;
        while (child != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(child), "Corrupted hierarchy: child does not have a basic_hierarchy component");
            auto& ch = reg.template get<basic_hierarchy>(child);
            const entity_type next_child = ch.next_sibling;
            ch.parent = ch.next_sibling = ch.prev_sibling = entt::null;
            child = next_child;
        }

        ph.first_child = ph.last_child = entt::null;
        ph.child_count = 0u;
    }

    /*!
     * @brief Inserts a child entity before another child entity in the parent's list of children.
     * @param reg The registry to operate on.
     * @param before The child entity before which to insert.
     * @param child The child entity to insert.
     */
	static void insert_before(registry_type& reg, const entity_type before, const entity_type child) {
        if (child == entt::null  || before == entt::null || before == child) {
			return;
		}

        ENTTX_ASSERT(reg.valid(child), "Child entity is not valid");
        ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(before), "Before entity does not have a basic_hierarchy component");

        auto& bh = reg.template get<basic_hierarchy>(before);
        const entity_type parent = bh.parent;

        if (parent == entt::null) {
            return;
        }

        detach(reg, child);

        ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(parent), "Corrupted hierarchy: parent does not have a basic_hierarchy component");
        ENTTX_ASSERT(!is_descendant(reg, parent, child), "Cannot insert a parent as a child of its descendant");

        auto& ph        = reg.template get<basic_hierarchy>(parent);
        auto& ch        = reg.template get_or_emplace<basic_hierarchy>(child);
        ch.parent       = parent;
        ch.next_sibling = before;
        ch.prev_sibling = bh.prev_sibling;

        if (bh.prev_sibling != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(bh.prev_sibling), "Corrupted hierarchy: previous sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(bh.prev_sibling).next_sibling = child;
        } else {
            // If there was no previous sibling, this child becomes the first child.
            ph.first_child = child;
        }
        
        bh.prev_sibling = child;
        ph.child_count++;
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

        ENTTX_ASSERT(reg.valid(child), "Child entity is not valid");
        ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(after), "After entity does not have a basic_hierarchy component");

        auto& ah = reg.template get<basic_hierarchy>(after);
        const entity_type parent = ah.parent;

        if (parent == entt::null) {
            return;
        }

        detach(reg, child);

        ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(parent), "Corrupted hierarchy: parent does not have a basic_hierarchy component");
        ENTTX_ASSERT(!is_descendant(reg, parent, child), "Cannot insert a parent as a child of its descendant");

        auto& ph        = reg.template get<basic_hierarchy>(parent);
        auto& ch        = reg.template get_or_emplace<basic_hierarchy>(child);
        ch.parent       = parent;
        ch.prev_sibling = after;
        ch.next_sibling = ah.next_sibling;

        if (ah.next_sibling != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(ah.next_sibling), "Corrupted hierarchy: next sibling does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(ah.next_sibling).prev_sibling = child;
        } else {
            // If there was no next sibling, this child becomes the last child.
            ph.last_child = child;
        }

        ah.next_sibling = child;
        ph.child_count++;
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

        ENTTX_ASSERT(reg.valid(parent), "Parent entity is not valid");
        ENTTX_ASSERT(reg.valid(child), "Child entity is not valid");

		detach( reg, child );

        ENTTX_ASSERT(!is_descendant(reg, parent, child), "Cannot push a parent as a child of its descendant");

		auto& ph = reg.template get_or_emplace<basic_hierarchy>(parent);
		auto& ch = reg.template get_or_emplace<basic_hierarchy>(child);

        ch.parent       = parent;
        ch.prev_sibling = entt::null;
        ch.next_sibling = ph.first_child;

        if (ph.first_child != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(ph.first_child), "Corrupted hierarchy: first child does not have a basic_hierarchy component");
            reg.template get<basic_hierarchy>(ph.first_child).prev_sibling = child;
        }
        
        ph.first_child = child;
        if (ph.last_child == entt::null)
            ph.last_child = child;
        
        ph.child_count++;
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

        ENTTX_ASSERT(reg.valid(parent), "Parent entity is not valid");
        ENTTX_ASSERT(reg.valid(child), "Child entity is not valid");

		detach( reg, child );

        ENTTX_ASSERT(!is_descendant(reg, parent, child), "Cannot push a parent as a child of its descendant");

		auto& ph = reg.template get_or_emplace<basic_hierarchy>(parent);
		auto& ch = reg.template get_or_emplace<basic_hierarchy>(child);

		ch.parent       = parent;
		ch.prev_sibling = ph.last_child;
		ch.next_sibling = entt::null;

		// If the parent already has a last child, update its next_sibling to point to the new child
		if (ph.last_child != entt::null) {
            ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(ph.last_child), "Corrupted hierarchy: last child does not have a basic_hierarchy component");
			reg.template get<basic_hierarchy>(ph.last_child).next_sibling = child;
		}

		ph.last_child = child;
		if (ph.first_child == entt::null)
			ph.first_child = child;

		ph.child_count++;
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
    static entity_type find_root(const registry_type& reg, entity_type entt) {
        entity_type root = entt;
        while (reg.template all_of<basic_hierarchy>(entt)) {
            const entity_type p = reg.template get<basic_hierarchy>(entt).parent;
            if (p == entt::null || !reg.template all_of<basic_hierarchy>(p))
                break;

            ENTTX_ASSERT(p != entt, "Cycle detected in hierarchy");
            root = p;
            entt = p;
        }
        return root;
    }

    /*!
     * @brief Checks if an entity is a descendant of another entity.
     * @param reg The registry containing the entities.
     * @param entt The entity to check.
     * @param ancestor The potential ancestor entity.
     * @return True if `entt` is a descendant of `ancestor`, false otherwise.
     */
    [[nodiscard]]
    static bool is_descendant(const registry_type& reg, entity_type entt, const entity_type ancestor) {
        while(reg.template all_of<basic_hierarchy>(entt)) {
            entt = reg.template get<basic_hierarchy>(entt).parent;
            if(entt == ancestor)
                return true;
        }

        return false;
    }

    /*! @brief Implements remap_traits (see prefab.hpp) */
    static void remap(registry_type& reg, entity_type entt, const auto& remap) {
        if (!reg.template all_of<basic_hierarchy>(entt)) {
            return;
        }

        auto& h        = reg.template get<basic_hierarchy>(entt);
        h.parent       = remap.translate(h.parent);
        h.first_child  = remap.translate(h.first_child);
        h.last_child   = remap.translate(h.last_child);
        h.next_sibling = remap.translate(h.next_sibling);
        h.prev_sibling = remap.translate(h.prev_sibling);
    }

    /*!
     * @brief Handles the destruction of an entity in the hierarchy.
     * @param reg The registry to operate on.
     * @param entt The entity being destroyed.
     * @note This takes advantage of the auto-connection of the `on_destroy` signal in EnTT. 
     */
    static void on_destroy(registry_type& reg, const entity_type entt) 
        requires (deletion_policy != hierarchy_deletion_policy::unhandled) {
        if (!reg.template all_of<basic_hierarchy>(entt)) {
            return;
        }

        detach(reg, entt);

        if constexpr (deletion_policy == hierarchy_deletion_policy::destroy_children) {
            entity_type child = reg.template get<basic_hierarchy>(entt).first_child;
            while (child != entt::null) {
                ENTTX_ASSERT(reg.template all_of<basic_hierarchy>(child), "Corrupted hierarchy: child does not have a basic_hierarchy component");
                const entity_type next = reg.template get<basic_hierarchy>(child).next_sibling;
                reg.template get<basic_hierarchy>(child).parent = entt::null; // skip patching entt, it's dying anyway
                reg.destroy(child);
                child = next;
            }
        } else if constexpr (deletion_policy == hierarchy_deletion_policy::orphan_children) {
            orphan_children(reg, entt);
        }
    }
};

using hierarchy = basic_hierarchy<entt::registry, 
                                  hierarchy_deletion_policy::destroy_children, 
                                  struct default_hierarchy_tag>;

} // namespace enttx