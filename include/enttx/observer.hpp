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

template<typename, typename>
struct change;

template<typename>
struct construct_change;

template<typename>
struct update_change;

template<typename>
struct destroy_change;

template<typename, typename>
class basic_observer;

template<typename T>
using observer = basic_observer<T, entt::registry>;

template<typename T> 
struct construct_change {
    const T value;

    void apply_to(entt::storage_for_t<T>& storage, const entt::entity entity) const {
        storage.emplace(entity, value);
    }
};

template<typename T> 
requires requires { entt::component_traits<T>::page_size == 0u; }
struct construct_change<T> {
    void apply_to(entt::storage_for_t<T>& storage, const entt::entity entity) const {
        storage.emplace(entity);
    }
};

template<typename T>
requires requires { entt::component_traits<T>::page_size != 0u; }
struct update_change<T> {
    const T value;

    void apply_to(entt::storage_for_t<T>& storage, const entt::entity entity) const {
        storage.patch(entity, [this](T& value) { value = this->value; });
    }
};

template<typename T>
struct destroy_change {
    void apply_to(entt::storage_for_t<T>& storage, const entt::entity entity) const {
        storage.erase(entity);
    }
};

template<typename T>
requires requires { entt::component_traits<T>::page_size == 0u; }
struct destroy_change<T> {
    void apply_to(entt::storage_for_t<T>& storage, const entt::entity entity) const {
        storage.erase(entity);
    }
};

template<typename T, typename Base = std::variant<construct_change<T>, update_change<T>, destroy_change<T>>>
struct change : Base {
    using Base::Base;

    void apply_to(entt::storage_for_t<T>& storage, const entt::entity entity) const {
        std::visit([&](const auto& change) { change.apply_to(storage, entity); }, *this);
    }
};

/*! @brief Tracks changes to components of type `T` within the given `Registry`. */
template<typename T, typename Registry>
class basic_observer {
public:
    std::vector<change<T>> changes;
};

} // namespace enttx