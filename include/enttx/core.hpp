/*!
 * @file core.hpp
 * @brief Core utilities for enttx.
 */

#pragma once
#include "config.hpp"
#include <entt/entity/component.hpp>

namespace enttx
{
    /*!
     * @brief Checks if a type T is pageless (i.e., has a page size of 0).
     * @tparam T The type to check.
     * @tparam Entity The entity type associated with the component traits.
     * 
     * Pageless components are those that do not occupy any storage space in the registry, 
     * typically because they are empty types. This concept is useful for optimizing 
     * operations on such components, but requires special handling when accessing them.
     */
    template<typename T, typename Entity = entt::entity>
    concept is_pageless = entt::component_traits<T, Entity>::page_size == 0u;
} // namespace enttx