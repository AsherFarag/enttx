#pragma once
#include <entt/fwd.hpp>
#include <unordered_map>

#include "config.hpp"

namespace enttx {

/*! @brief A class for remapping entities from one registry to another. */
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
        if (old == entt::null) {
            return entt::null;
        }
        if (auto it = entt_map.find(old); it != entt_map.end()) {
            return it->second;
        }
        return entt::null;
    }

    std::unordered_map<entity_type, entity_type> entt_map;
};

/*! @brief Checks if a type T has a static member function `remap` accepting `Registry`. */
template<typename T, typename Registry>
concept has_member_remap = requires(Registry& reg, typename Registry::entity_type e, const basic_entity_remap<Registry>& remap) {
    { T::remap(reg, e, remap) } -> std::same_as<void>;
};

/*!
 * @brief Provides a default implementation of remap_traits for types that have a static member function `remap`.
 * @tparam T The type to check for a static member function `remap`.
 * 
 * Specialize this type to provide a custom implementation of remap for a specific type T.
 */
template<typename T>
struct remap_traits {
    template<typename Registry> 
    requires has_member_remap<T, Registry>
    static void remap(Registry& reg, typename Registry::entity_type e, const basic_entity_remap<Registry>& remap) {
        T::remap(reg, e, remap);
    }
};

/*! @brief Checks if a type T has a valid `remap_traits` specialization. */
template<typename T, typename Registry>
concept is_remappable = requires(Registry& reg, typename Registry::entity_type e, const basic_entity_remap<Registry>& remap) {
    { remap_traits<T>::remap(reg, e, remap) } -> std::same_as<void>;
};

/*! @brief Alias declaration for the most common use case. */
using entity_remap = basic_entity_remap<entt::registry>;

} // namespace enttx