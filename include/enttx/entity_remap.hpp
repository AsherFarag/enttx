#pragma once
#include "config.hpp"

#include <entt/fwd.hpp>
#include <entt/container/dense_map.hpp> // For entt::dense_map used in basic_entity_remap

#include <concepts>

namespace enttx {

/*! @brief Checks if T is a valid remapper (functor that takes in a entity and returns an entity) */
template<typename T, typename Registry = entt::registry>
concept is_entity_remapper = requires(const T& remapper, typename Registry::entity_type e) {
    { remapper(e) } -> std::same_as<typename Registry::entity_type>;
};

/*! @brief Checks if a type T has a static member function `remap` satisfying the expected signature. */
template<typename T, typename Registry, typename Remapper>
concept has_member_remap =
    is_entity_remapper<Remapper, Registry> &&
    requires(
        Registry& reg,
        typename Registry::entity_type e,
        const Remapper& remap
    ) {
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
    template<typename Registry, typename Remapper>
    requires has_member_remap<T, Registry, Remapper>
    static void remap(
        Registry& reg,
        typename Registry::entity_type e,
        const Remapper& remapper
    )
    {
        T::remap(reg, e, remapper);
    }
};

/*! @brief Checks if a type T has a valid `remap_traits` specialization. */
template<typename T, typename Registry, typename Remapper>
concept is_remappable =
    is_entity_remapper<Remapper, Registry> &&
    requires(
        Registry& reg,
        typename Registry::entity_type e,
        const Remapper& remapper
    ) {
        {
            remap_traits<T>::remap(reg, e, remapper)
        } -> std::same_as<void>;
    };

/*! 
 * @brief A class for remapping old entity identifiers to new ones.

 * @code{.cpp}
 * auto remap = enttx::basic_entity_remap<...>{}
 *      .map(old_entity, new_entity);
 * 
 * auto remapped_entity = remap(old_entity);
 * // remapped_entity == new_entity
 * @endcode
 */
template<typename Entity>
class basic_entity_remap {
    using traits_type = entt::entt_traits<Entity>;
public:
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;
    /*! @brief Underlying version type. */
    using version_type = typename traits_type::version_type;

    /*! @brief Maps an old entity identifier to a new one. Returns itself for method chaining. */
    basic_entity_remap& map(entity_type old, entity_type new_entity) {
        entt_map[old] = new_entity;
        return *this;
    }

    /*! @brief Translates an old entity identifier to a new one. Returns entt::null if the old entity is not mapped. */
    [[nodiscard]]
    entity_type operator()(entity_type old) const noexcept {
        if (old == entt::null) {
            return entt::null;
        }
        if (auto it = entt_map.find(old); it != entt_map.end()) {
            return it->second;
        }
        return entt::null;
    }

    entt::dense_map<entity_type, entity_type> entt_map;
};

/*! @brief Alias declaration for the most common use case. */
using entity_remap = basic_entity_remap<entt::entity>;

} // namespace enttx