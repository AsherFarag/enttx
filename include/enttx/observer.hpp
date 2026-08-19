/*!
 * @file observer.hpp
 * @brief Observer utility for enttx.
 * @dependencies change_mixin.hpp
 */

#pragma once
#include "config.hpp"
#include "entity_remap.hpp"

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

template<typename Commit>
class basic_commit_serializer;

using commit_serializer = basic_commit_serializer<commit>;

template<typename Commit>
class basic_commit_deserializer;

using commit_deserializer = basic_commit_deserializer<commit>;

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
    using variant_type = std::variant<construct_change<T>, destroy_change<T>, update_change<T>>;

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
    void apply_to(entt::storage_for_t<T, Entity, Args...>& storage, const Entity target = entt::null) const {
        const Entity entity = target == entt::null ? this->entity : target;
        std::visit([&storage, entity] (const auto& change) { 
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
class basic_commit {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

    friend class basic_commit_serializer<basic_commit<Registry>>;
    friend class basic_commit_deserializer<basic_commit<Registry>>;

public:
    /*! @brief Type of registry */ 
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;

    using entity_remap_type = basic_entity_remap<entity_type, entity_type>;

	[[nodiscard]]
    basic_commit invert() const {
        basic_commit inverted;
        for (const auto& [storage_id, segment] : segments) {
            inverted.segments[storage_id] = segment->invert();
        }
        return inverted;
    }

    void apply(Registry& registry, const entity_remap_type* remap = nullptr) const {
        for (const auto& [storage_id, segment] : segments) {
            segment->apply(registry, storage_id, remap);
        }
    }

    template<typename T>
    void append_segment(std::vector<change<T, entity_type>>&& changes, const entt::id_type storage_id = entt::type_hash<T>::value()) {
        auto s = std::make_unique<segment<T>>();
        s->changes = std::move(changes);
        segments[storage_id] = std::move(s);
    }

private:
    struct segment_base {
        virtual ~segment_base() = default;
        virtual std::unique_ptr<segment_base> invert() const = 0;
        virtual void apply(Registry& registry, const entt::id_type storage_id, const entity_remap_type* remap = nullptr) const = 0;
    };

    template<typename T>
    struct segment final: public segment_base {
        using change_list_type = std::vector<change<T, entity_type>>;

        change_list_type changes;

        std::unique_ptr<segment_base> invert() const override {
            auto inverted = std::make_unique<segment<T>>();
            inverted->changes.reserve(changes.size());
            for (auto rit = changes.rbegin(); rit != changes.rend(); ++rit) {
                inverted->changes.emplace_back(rit->invert());
            }
            return inverted;
        }

        void apply(Registry& registry, const entt::id_type storage_id, const entity_remap_type* remap = nullptr) const override {
            auto& storage = registry.template storage<T>(storage_id);
            if (remap) {
                for (const auto& change : changes) {
                    const auto remapped_entity = (*remap)(change.entity);
                    if (remapped_entity != entt::null) {
                        change.apply_to(storage, remapped_entity);
                    }
                }
            } else {
                for (const auto& change : changes) {
                    change.apply_to(storage);
                }
            }
        }
    };

    entt::dense_map<entt::id_type, std::unique_ptr<segment_base>> segments;
};

template<typename Commit>
class basic_commit_serializer {
public:
    using commit_type = Commit;

    basic_commit_serializer(const commit_type& commit) noexcept
        : commit{&commit} {}

    basic_commit_serializer(const basic_commit_serializer&) = delete;

    basic_commit_serializer(basic_commit_serializer&&) noexcept = default;

    ~basic_commit_serializer() = default;

    basic_commit_serializer& operator=(const basic_commit_serializer&) = delete;

    basic_commit_serializer& operator=(basic_commit_serializer&&) noexcept = default;

    template<typename T, typename Archive>
    const basic_commit_serializer& get(Archive& archive, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        using segment_type = typename commit_type::template segment<T>;
        const auto it = commit->segments.find(storage_id);

        if (it == commit->segments.end()) {
            archive(std::size_t{0}); // No changes for this storage_id, serialize as empty
            return *this;
        }

        const auto& changes = static_cast<const segment_type&>(*it->second).changes;
        archive(changes.size());

        for (const auto& change : changes) {
            archive(change.entity); // TODO: We should use a remapper here. For example, mapping the entt::entity to a network_id etc.
            archive(static_cast<std::uint8_t>(change.payload.index()));

            std::visit([&archive](const auto& c) {
                using change_type = std::decay_t<decltype(c)>;
                if constexpr(std::is_same_v<change_type, update_change<T>>) {
                    archive(c.old_value, c.new_value);
                } else if constexpr(!is_pageless<T>) {
                    archive(c.value); // construct_change<T> / destroy_change<T>
                }
            }, change.payload);
        }

        return *this;
    }

private:
    const commit_type* commit;
};

template<typename Commit>
struct basic_commit_deserializer {
public:
    using commit_type = Commit;

    basic_commit_deserializer(commit_type& commit) noexcept
        : commit{&commit} {}

    basic_commit_deserializer(const basic_commit_deserializer&) = delete;

    basic_commit_deserializer(basic_commit_deserializer&&) noexcept = default;

    ~basic_commit_deserializer() = default;

    basic_commit_deserializer& operator=(const basic_commit_deserializer&) = delete;

    basic_commit_deserializer& operator=(basic_commit_deserializer&&) noexcept = default;

    template<typename T, typename Archive>
    const basic_commit_deserializer& get(Archive& archive, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        return *this;
    }

private:
    commit_type* commit;
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
            commit.template append_segment<value_type>(std::move(changes), storage_id);
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