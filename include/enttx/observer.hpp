/*!
 * @file observer.hpp
 * @brief Observer utility for enttx.
 * @dependencies change_mixin.hpp
 */

#pragma once
#include "config.hpp"

#include <entt/entity/fwd.hpp>

#include <variant>
#include <vector>

namespace enttx
{

// TODO: Should be in a core.hpp? 
template<typename T>
concept is_pageless = entt::component_traits<T>::page_size == 0u;

template<typename>
struct construct_change;

template<typename T>
struct update_change;

template<typename>
struct destroy_change;

template<typename, typename = entt::entity>
struct change;

template<typename Storage, typename Allocator = std::allocator<change<typename Storage::value_type, typename Storage::entity_type>>>
class basic_observer;

template<typename T, typename... Args>
using observer = basic_observer<entt::storage_for_t<T, entt::entity>, Args...>;

template<typename T> 
struct construct_change {
    /*! @brief Value type of the component being constructed. */
    using value_type = T;

    const T value;

    [[nodiscard]] destroy_change<T> invert() const {
        return {value};
    }
};

template<is_pageless T>
struct construct_change<T> {
    /*! @brief Value type of the component being constructed. */
    using value_type = T;

    [[nodiscard]] destroy_change<T> invert() const {
        return {};
    }
};

template<typename T> 
requires (!is_pageless<T>)
struct update_change<T> {
    /*! @brief Value type of the component being updated. */
    using value_type = T;

    // TODO: These should probably be const but i need them to be mutable for now for the observer::update
    T old_value, new_value;

    [[nodiscard]] update_change<T> invert() const {
        return {new_value, old_value};
    }
};

template<typename T>
struct destroy_change {
    /*! @brief Value type of the component being destroyed. */
    using value_type = T;

    const T value;

    [[nodiscard]] construct_change<T> invert() const {
        return {value};
    }
};

template<is_pageless T>
struct destroy_change<T> {
    /*! @brief Value type of the component being destroyed. */
    using value_type = T;

    [[nodiscard]] construct_change<T> invert() const {
        return {};
    }
};

/*!
 * @brief Represents a change to a component of type `T` associated with an entity of type `Entity`.
 * @tparam T Type of the component being changed.
 * @tparam Entity Type of the entity associated with the change.
 */
template<typename T, typename Entity = entt::entity>
struct change {
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Type of the change being tracked. */
    using variant_type = std::variant<construct_change<T>, update_change<T>, destroy_change<T>>;

    entity_type entity;
    variant_type payload;

    [[nodiscard]] change<T, Entity> invert() const {
        return {entity, std::visit([](const auto& change) { return change.invert(); }, payload)};
    }

    template<typename Entity = entt::entity, typename... Args>
    void apply_to(entt::storage_for_t<T, Entity, Args...>& storage) const {
        std::visit([&storage, entity](const auto& change) { 
            using ChangeType = std::decay_t<decltype(change)>;

            // construct
            if constexpr(std::is_same_v<ChangeType, construct_change<T>>) {
                if constexpr(is_pageless<T>) {
                    storage.emplace(entity);
                } else {
                    storage.emplace(entity, change.value);
                }
            // update
            } else if constexpr(std::is_same_v<ChangeType, update_change<T>>) {
                if constexpr(!is_pageless<T>) {
                    storage.patch(entity, [&change](T& value) { value = change.new_value; });
                }
            // destroy
            } else if constexpr(std::is_same_v<ChangeType, destroy_change<T>>) {
                storage.erase(entity);
            // unknown
            } else {
                static_assert(std::false_type::value, "Unknown change type - Did you forget to add a specialization for your change type?");
            }
        }, payload);
    }
};

/*! @brief Tracks changes to components of type `T` within the given `Registry`. */
template<typename Storage, typename Allocator>
class basic_observer {
public:
    /*! @brief Storage for the component type being observed. */
    using storage_type = Storage;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename storage_type::entity_type;
    /*! @brief Value type of the component being observed. */
    using value_type = typename storage_type::value_type;
    /*! @brief Allocator type for the changes vector. */
    using allocator_type = Allocator;
    /*! @brief Type of the change being tracked. */
    using change_type = change<value_type, entity_type>;
    /*! @brief Container for the changes being tracked. */
    using change_list_type = std::vector<change_type, allocator_type>;

    storage_type& storage;
    change_list_type changes;

    basic_observer(storage_type& storage, const allocator_type& allocator = allocator_type{})
        : storage{storage}, changes{allocator} {
        storage.on_construct().template connect<&basic_observer::on_construct>(*this);
        if constexpr(is_pageless<value_type>) {
            storage.on_pre_update().template connect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template connect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template connect<&basic_observer::on_destroy>(*this);
    }

    ~basic_observer() {
        storage.on_construct().template disconnect<&basic_observer::on_construct>(*this);
        if constexpr(is_pageless<value_type>) {
            storage.on_pre_update().template disconnect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template disconnect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template disconnect<&basic_observer::on_destroy>(*this);
    }

protected:
    void on_construct(const entity_type entity) {
        if constexpr(is_pageless<value_type>) {
            changes.emplace_back(entity, construct_change<value_type>{});
        } else {
            changes.emplace_back(entity, construct_change<value_type>{storage.get(entity)});
        }
    }

    void on_pre_update(const entity_type entity) requires(!is_pageless<value_type>) {
        changes.emplace_back(entity, update_change<value_type>{ .old_value = storage.get(entity) });
    }

    void on_update(const entity_type entity) requires(!is_pageless<value_type>) {
        // TODO: Figure out if this is safe and the right way to do it.
        // Need to check if its legal that patch cannot modify the registry in a way that would invalidate this.
        changes.back().second.new_value = storage.get(entity);
    }

    void on_destroy(const entity_type entity) {
        changes.emplace_back(entity, destroy_change<value_type>{});
    }
};

} // namespace enttx