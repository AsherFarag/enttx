/*!
 * @file observer.hpp
 * @brief Observer utility for enttx.
 * @dependencies change_mixin.hpp
 */

#pragma once
#include "config.hpp"

#include <entt/entity/fwd.hpp>

#include <variant>

namespace enttx
{

// TODO: Should be in a core.hpp? 
template<typename T>
concept is_pageless = entt::component_traits<T>::page_size == 0u;

template<typename>
struct construct_change;

template<typename>
struct update_change;

template<typename>
struct destroy_change;

template<typename T, typename = std::variant<construct_change<T>, update_change<T>, destroy_change<T>>>
struct change;

template<typename, typename = entt::entity, typename...>
class basic_observer;

template<typename T>
using observer = basic_observer<T, entt::registry>;

template<typename T> 
struct construct_change {
    const T value;
};

template<typename T>
requires requires { entt::component_traits<T>::page_size != 0u; }
struct update_change<T> {
    T old_value, new_value;
};

template<typename T>
struct destroy_change {
};

template<typename T, typename Base>
struct change : Base {
    using Base::Base;

    template<typename Entity = entt::entity, typename... Args>
    void apply_to(entt::storage_for_t<T, Entity, Args...>& storage, const Entity entity) const {
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
        }, static_cast<const Base&>(*this));
    }
};

/*! @brief Tracks changes to components of type `T` within the given `Registry`. */
template<typename T, typename Entity, typename... Args>
class basic_observer {
public:
    /*! @brief Storage for the component type being observed. */
    using storage_type = entt::storage_for_t<T, Entity, Args...>;
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief */
    using change_type = change<T>;

    storage_type& storage;
    std::vector<std::pair<entity_type, change_type>> changes;

    void connect() {
        storage.on_construct().template connect<&basic_observer::on_construct>(*this);
        if constexpr(is_pageless<T>) {
            storage.on_pre_update().template connect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template connect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template connect<&basic_observer::on_destroy>(*this);
    }

    void disconnect() {
        storage.on_construct().template disconnect<&basic_observer::on_construct>(*this);
        if constexpr(is_pageless<T>) {
            storage.on_pre_update().template disconnect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template disconnect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template disconnect<&basic_observer::on_destroy>(*this);
    }

protected:
    void on_construct(const entity_type entity) {
        if constexpr(is_pageless<T>) {
            changes.emplace_back(entity, construct_change<T>{});
        } else {
            changes.emplace_back(entity, construct_change<T>{storage.get(entity)});
        }
    }

    void on_pre_update(const entity_type entity) requires(!is_pageless<T>) {
        changes.emplace_back(entity, update_change<T>{ .old_value = storage.get(entity) });
    }

    void on_update(const entity_type entity) requires(!is_pageless<T>) {
        // TODO: Figure out if this is safe and the right way to do it.
        // Need to check if its legal that patch cannot modify the registry in a way that would invalidate this.
        changes.back().second.new_value = storage.get(entity);
    }

    void on_destroy(const entity_type entity) {
        changes.emplace_back(entity, destroy_change<T>{});
    }

};

} // namespace enttx