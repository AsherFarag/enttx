/*!
 * @file observer.hpp
 * @brief Observer utility for enttx.
 * @dependencies change_mixin.hpp
 */

#pragma once
#include "config.hpp"

#include <entt/entity/fwd.hpp>
#include <entt/signal/fwd.hpp>

#include <variant>
#include <vector>
#include <memory>

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

template<typename>
class basic_commit;

using commit = basic_commit<entt::registry>;

template<typename T> 
struct construct_change {
    /*! @brief Value type of the component being constructed. */
    using value_type = T;

    T value;

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

    T value;

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
template<typename T, typename Entity>
struct change {
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Type of the change being tracked. */
    using variant_type = std::variant<construct_change<T>, update_change<T>, destroy_change<T>>;

    entity_type entity;
    variant_type payload;

    [[nodiscard]] change invert() const {
		change inverted;
        inverted.entity = this->entity;
        std::visit([&inverted](const auto& change) { 
            inverted.payload = change.invert();
        }, payload);
        return inverted;
    }

    template<typename Entity = entt::entity, typename... Args>
    void apply_to(entt::storage_for_t<T, Entity, Args...>& storage) const {
        std::visit([&storage, entity = this->entity](const auto& change) { 
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

template<typename Registry>
class basic_commit final {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
    /*! @brief Type of registry */ 
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;

    struct segment_base {
        entt::id_type storage_id;
        virtual ~segment_base() = default;
        virtual std::unique_ptr<segment_base> invert() const = 0;
        virtual void apply(Registry& registry) const = 0;
    };

    template<typename T>
    struct segment final: public segment_base {
        using change_list_type = std::vector<change<T, entity_type>>;

        change_list_type changes;

        std::unique_ptr<segment_base> invert() const override {
            auto inverted = std::make_unique<segment<T>>();
            inverted->storage_id = this->storage_id;
            inverted->changes.reserve(changes.size());
            for (auto rit = changes.rbegin(); rit != changes.rend(); ++rit) {
                inverted->changes.emplace_back(rit->invert());
            }
            return inverted;
        }

        void apply(Registry& registry) const override {
            auto& storage = registry.template storage<T>(this->storage_id);
            for (const auto& change : changes) {
                change.apply_to(storage);
            }
        }
    };

    std::vector<std::unique_ptr<segment_base>> segments;

	[[nodiscard]]
    basic_commit invert() const {
        basic_commit inverted;
        for (const auto& segment : segments) {
            inverted.segments.emplace_back(segment->invert());
        }
        return inverted;
    }

    void apply(Registry& registry) const {
        for (const auto& segment : segments) {
            segment->apply(registry);
        }
    }

    template<typename T>
    void append_segment(entt::id_type storage_id, std::vector<change<T, entity_type>> changes) {
        auto s = std::make_unique<segment<T>>();
        s->storage_id = storage_id;
        s->changes = std::move(changes);
        segments.emplace_back(std::move(s));
    }
};

template<typename Registry>
class basic_observer_base {
public:
    using commit_type = basic_commit<Registry>;
    
    virtual ~basic_observer_base() = default;

	virtual void connect() = 0;
	virtual void disconnect() = 0;
    virtual void collect(commit_type& commit) = 0;
};

template<typename Storage>
concept is_change_observer_storage = requires (Storage& s) {
    typename Storage::value_type;
    typename Storage::entity_type;
    { s.on_construct() };
    { s.on_destroy() };
}
&& (
    is_pageless<typename Storage::value_type> 
    ||
    requires (Storage& s) {
        { s.on_pre_update() };
        { s.on_update() };
    }
);

/*! @brief Tracks changes to components of type `T` within the given `Registry`. */
template<is_change_observer_storage Storage, typename Allocator = std::allocator<change<typename Storage::value_type, typename Storage::entity_type>>>
class basic_observer final: public basic_observer_base<typename Storage::registry_type> {
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
    /*! @brief Base type for commits. */
    using commit_base_type = basic_commit<typename storage_type::registry_type>;

    entt::id_type storage_id;
    storage_type& storage;
    change_list_type changes;

    basic_observer(entt::id_type storage_id, storage_type& storage, const allocator_type& allocator = allocator_type{})
        : storage_id{storage_id}, storage{storage}, changes{allocator} {
        connect();
    }

    ~basic_observer() {
        disconnect();
    }

    void connect() override {
        storage.on_construct().template connect<&basic_observer::on_construct>(*this);
        if constexpr(!is_pageless<value_type>) {
            storage.on_pre_update().template connect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template connect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template connect<&basic_observer::on_destroy>(*this);
    }

    void disconnect() override {
        storage.on_construct().template disconnect<&basic_observer::on_construct>(*this);
        if constexpr(!is_pageless<value_type>) {
            storage.on_pre_update().template disconnect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template disconnect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template disconnect<&basic_observer::on_destroy>(*this);
    }

    void collect(commit_base_type& commit) override {
        if (!changes.empty()) {
            commit.template append_segment<value_type>(storage_id, std::move(changes));
            changes.clear();
        }
    }

private:
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
        std::get<update_change<value_type>>(changes.back().payload).new_value = storage.get(entity);
    }

    void on_destroy(const entity_type entity) {
        if constexpr(is_pageless<value_type>) {
            changes.emplace_back(entity, destroy_change<value_type>{});
        } else {
            changes.emplace_back(entity, destroy_change<value_type>{storage.get(entity)});
        }
    }
};

template<typename T, typename Registry>
[[nodiscard]] auto observe(Registry& registry, entt::id_type storage_id = entt::type_hash<T>::value()) {
    auto& storage = registry.template storage<T>(storage_id);
    using storage_type = std::remove_reference_t<decltype(storage)>;
    return basic_observer<storage_type>{storage_id, storage};
}

} // namespace enttx