/*!
 * @file observer.hpp
 * @brief Observer utility for enttx.
 */

// TODO: Not all changes need to be invertible. Implement an api where users can specify if they want the changes to be invertible or not. 
// For example, if commits are being sent over the network and inversions are not needed,
// then we can save some memory and bandwidth by not storing the old value for updates and destructs. 

#pragma once
#include "core.hpp"
#include "entity_remap.hpp"

#include <entt/core/type_info.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/signal/fwd.hpp>
#include <entt/container/dense_map.hpp>

#include <variant>
#include <vector>
#include <memory>
#include <optional>

namespace enttx
{

template<typename, typename = entt::entity>
struct change;

template<typename Registry>
class basic_observer_base;

template<typename Registry>
using basic_observers = std::vector<std::unique_ptr<basic_observer_base<Registry>>>;

template<typename>
class basic_commit;

template<typename Commit>
class basic_commit_snapshot;

template<typename Commit>
class basic_commit_loader;

/*! @brief Alias declaration for the most common use case. */
using observer_base = basic_observer_base<entt::registry>;

/*! @brief Alias declaration for the most common use case. */
using observers = basic_observers<entt::registry>;

/*! @brief Alias declaration for the most common use case. */
using commit = basic_commit<entt::registry>;

/*! @brief Alias declaration for the most common use case. */
using commit_snapshot = basic_commit_snapshot<commit>;

/*! @brief Alias declaration for the most common use case. */
using commit_loader = basic_commit_loader<commit>;

/*!
 * @brief Base class for observers that track changes in a registry.
 * @tparam Registry Type of the registry being observed.
 */
template<typename Registry>
class basic_observer_base {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
    /*! @brief Type of registry */ 
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;
    /*! @brief Commit type associated with this observer. */
    using commit_type = basic_commit<Registry>;
    
    /*! @brief Default constructor. `connect()` is expected to be called within the derived class constructor. */
    basic_observer_base() = default;
    /*! @brief Virtual destructor. `disconnect()` is expected to be called within the derived class destructor. */
    virtual ~basic_observer_base() = default;

    /*! @brief Connects the observer events to the registry. */
	virtual void connect() = 0;
    
    /*! @brief Disconnects the observer events from the registry. */
	virtual void disconnect() = 0;

    /*! @brief Collects the changes observed since the last collection and stores them in the provided commit. */
    virtual void collect(commit_type& commit) = 0;
};

/*!
 * @brief Represents a change to a component of type `T` associated with an entity of type `Entity`.
 * @tparam T Type of the component being changed.
 * @tparam Entity Type of the entity associated with the change.
 */
template<typename T, typename Entity>
struct change {
    struct construct { T value; };

    struct destruct { T value; };

    struct update { T old_value, new_value; };

    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Type of the change being tracked. */
	using variant_type = std::variant<construct, destruct, update>;

    entity_type entity;
    variant_type payload;

    [[nodiscard]] change invert() const {
		change inverted;
        inverted.entity = this->entity;
        std::visit([&inverted](const auto& change) { 
            using ChangeType = std::decay_t<decltype(change)>;
            if constexpr(std::is_same_v<ChangeType, construct>) {
                inverted.payload = destruct{change.value};
            } else if constexpr(std::is_same_v<ChangeType, destruct>) {
                inverted.payload = construct{change.value};
            } else if constexpr(std::is_same_v<ChangeType, update>) {
                inverted.payload = update{change.new_value, change.old_value};
            } else {
                static_assert(std::false_type::value, "Unknown change type");
            }
        }, payload);
        return inverted;
    }

    template<typename... Args>
    void apply_to(entt::storage_for_t<T, entity_type, Args...>& storage, const entity_type target = entt::null) const {
        const entity_type entity = target == entt::null ? this->entity : target;
        std::visit([&storage, entity] (const auto& change) { 
            using ChangeType = std::decay_t<decltype(change)>;
            if constexpr(std::is_same_v<ChangeType, construct>) {
                storage.emplace(entity, change.value);
            } else if constexpr(std::is_same_v<ChangeType, update>) {
                storage.patch(entity, [&change](T& value) { value = change.new_value; });
            } else if constexpr(std::is_same_v<ChangeType, destruct>) {
                storage.erase(entity);
            } else {
                static_assert(std::false_type::value, "Unknown change type");
            }
        }, payload);
    }
};

/*! @brief Specialization of `change` for pageless components, which do not have a value associated with them. */
template<typename T, typename Entity>
requires is_pageless<T, Entity>
struct change<T, Entity> {
    struct construct {};

    struct destruct {};

    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Type of the change being tracked. */
	using variant_type = std::variant<construct, destruct>;

    entity_type entity;
    variant_type payload;

    [[nodiscard]] change invert() const {
		change inverted;
        inverted.entity = entity;
        if (std::holds_alternative<construct>(payload)) {
            inverted.payload = destruct{};
        } else /*if destruct*/ {
            inverted.payload = construct{};
        }
        return inverted;
    }

    template<typename... Args>
    void apply_to(entt::storage_for_t<T, entity_type, Args...>& storage, const entity_type target = entt::null) const {
        const entity_type entity = target == entt::null ? this->entity : target;
        if (std::holds_alternative<construct>(payload)) {
            storage.emplace(entity);
        } else /*if destruct*/ {
            storage.erase(entity);
        }
    }
};

/*!
 * @brief Represents a collection of changes (a commit) that can be applied to a registry.
 * @tparam Registry Type of the registry to which the commit can be applied.
 */
template<typename Registry>
class basic_commit {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

    friend class basic_commit_snapshot<basic_commit>;
    friend class basic_commit_loader<basic_commit>;

public:
    /*! @brief Type of registry */ 
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;

    using entity_remap_type = basic_entity_remap<entity_type, entity_type>;

    /*! @brief Inverts the commit, producing a new commit that can undo the changes of this commit. */
	[[nodiscard]] basic_commit invert() const {
        basic_commit inverted;
        for (const auto& [storage_id, segment] : segments) {
            inverted.segments[storage_id] = segment->invert();
        }
        return inverted;
    }

    /*! 
     * @brief Applies the commit to the given registry.
     * @param registry The registry to which the commit will be applied.
     * @param remap Optional pointer to an entity remap that can be used to remap entities during the application of the commit.
     * @remark If a remap is provided, entities in the commit will be remapped according to the remap before being applied to the registry.
     *         If no remap is provided, entities will be applied as-is.
     */
    void apply(Registry& registry, const entity_remap_type* remap = nullptr) const {
        for (const auto& [storage_id, segment] : segments) {
            segment->apply(registry, storage_id, remap);
        }
    }

    // TODO: This API is a hacky and not user friendly.
    template<typename T, typename Allocator>
    void append_segment(std::vector<change<T, entity_type>, Allocator>&& changes, const entt::id_type storage_id = entt::type_hash<T>::value()) {
        if (const auto it = segments.find(storage_id); it != segments.end()) {
            auto& existing = static_cast<segment<T>&>(*it->second).changes;
            existing.insert(existing.end(), std::make_move_iterator(changes.begin()), std::make_move_iterator(changes.end()));
            return;
        }
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

/*!
 * @brief Utility class to serialize a commit into an archive, with optional entity remapping.
 * @tparam Commit Type of the commit to be serialized.
 */
template<typename Commit>
class basic_commit_snapshot {
    static_assert(!std::is_const_v<Commit>, "Non-const commit type required");

public:
    /*! @brief Basic commit type. */
    using commit_type = Commit;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename commit_type::entity_type;

    /*!
     * @brief Constructs an instance that is bound to a given commit.
     * @param commit A valid reference to a commit.
     */
    basic_commit_snapshot(const commit_type& commit) noexcept
        : commit{&commit} {}

    /*! @brief Default copy constructor, deleted on purpose. */
    basic_commit_snapshot(const basic_commit_snapshot&) = delete;

    /*! @brief Default move constructor. */
    basic_commit_snapshot(basic_commit_snapshot&&) noexcept = default;

    /*! @brief Default destructor. */
    ~basic_commit_snapshot() = default;

    /**
     * @brief Default copy assignment operator, deleted on purpose.
     * @return This snapshot.
     */
    basic_commit_snapshot& operator=(const basic_commit_snapshot&) = delete;

    /**
     * @brief Default move assignment operator.
     * @return This snapshot.
     */
    basic_commit_snapshot& operator=(basic_commit_snapshot&&) noexcept = default;

    /*!
     * @brief Serializes the changes for a specific component type.
     *
     * @tparam T The component type.
     * @tparam Archive The archive type.
     * @tparam EntityHandler A callable that maps entities to a serializable form. E.g., entt::entity -> network_id.
     * @param archive The archive to serialize to.
     * @param entity_handler The entity handler callable.
     * @param storage_id The storage identifier, defaults to the type hash of T.
     * @return A const reference to this snapshot.
     * 
     * @remark The entity_handler is expected to be a callable that takes an entity of type `entity_type` 
     *         and returns a serializable representation, which can be anything, even just the entity itself.
     *         But it must produce a value the load-side can use to remap the entity back to the correct entity in the registry.
     */
    template<typename T, typename Archive, std::invocable<entity_type> EntityHandler>
    const basic_commit_snapshot& get(Archive& archive, EntityHandler&& entity_handler, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        using segment_type = typename commit_type::template segment<T>;
        const auto it = commit->segments.find(storage_id);

        if (it == commit->segments.end()) {
            archive(std::size_t{0}); // No changes for this storage_id, serialize as empty
            return *this;
        }

        const auto& changes = static_cast<const segment_type&>(*it->second).changes;
        archive(changes.size());

        for (const auto& change : changes) {
            using change_type = std::decay_t<decltype(change)>;
            
            archive(entity_handler(change.entity));
            archive(static_cast<std::uint8_t>(change.payload.index()));

            if constexpr(!is_pageless<T, entity_type>) {
                std::visit([&archive](const auto& c) {
                    if constexpr(std::is_same_v<std::decay_t<decltype(c)>, typename change_type::update>) {
                        archive(c.old_value, c.new_value);
                    } else {
                        archive(c.value); // construct / destroy
                    }
                }, change.payload);
            }
        }

        return *this;
    }

    template<typename T, typename Archive>
    const basic_commit_snapshot& get(Archive& archive, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        return get<T>(archive, [](const entity_type entity) { return entity; }, storage_id);
    }

private:
    const commit_type* commit;
};

template<typename Commit>
basic_commit_snapshot(const Commit& commit) -> basic_commit_snapshot<Commit>;

/*!
 * @brief Utility class to deserialize a commit from an archive, with optional entity remapping.
 * @tparam Commit Type of the commit to be deserialized.
 */
template<typename Commit>
struct basic_commit_loader {
public:
    /*! @brief Basic commit type. */
    using commit_type = Commit;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename commit_type::entity_type;


    /*!
     * @brief Constructs an instance that is bound to a given commit.
     * @param commit A valid reference to a commit.
     */
    basic_commit_loader(commit_type& commit) noexcept
        : commit{&commit} {}

    /*! @brief Default copy constructor, deleted on purpose. */
    basic_commit_loader(const basic_commit_loader&) = delete;

    /*! @brief Default move constructor. */
    basic_commit_loader(basic_commit_loader&&) noexcept = default;

    /*! @brief Default destructor. */
    ~basic_commit_loader() = default;

    /**
     * @brief Default copy assignment operator, deleted on purpose.
     * @return This loader.
     */
    basic_commit_loader& operator=(const basic_commit_loader&) = delete;

    /**
     * @brief Default move assignment operator.
     * @return This loader.
     */
    basic_commit_loader& operator=(basic_commit_loader&&) noexcept = default;

    /*!
     * @brief Deserializes the changes for a specific component type.
     * @tparam T Component type.
     * @tparam Archive Archive type.
     * @tparam EntityHandler Callable type to handle entity remapping.
     * @param archive Archive to deserialize from.
     * @param entity_handler Callable to handle entity remapping.
     * @param storage_id Optional storage identifier.
     * @return This loader.
     */
    template<typename T, typename Archive, typename EntityHandler> 
    requires (std::is_invocable_r_v<typename commit_type::entity_type, EntityHandler, Archive&>)
    const basic_commit_loader& get(Archive& archive, EntityHandler&& entity_handler, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        using change_type = change<T, entity_type>;

        std::size_t size{};
        archive(size);

        if (size == 0u) {
            return *this;
        }

        std::vector<change_type> changes;
        changes.reserve(size);

        for (std::size_t i = 0; i < size; ++i) {
            entity_type entity = entity_handler(archive);

            std::uint8_t index{};
            archive(index);

            typename change_type::variant_type payload;

            // Index order must match change<T>::variant_type: construct(0), destroy(1), update(2).
            switch (index) {
            case 0: {
                using construct = typename change_type::construct;
                if constexpr(is_pageless<T, entity_type>) {
                    payload = construct{};
                } else {
                    T value{};
                    archive(value);
                    payload = construct{std::move(value)};
                }
                break;
            }
            case 1: {
                using destruct = typename change_type::destruct;
                if constexpr(is_pageless<T, entity_type>) {
                    payload = destruct{};
                } else {
                    T value{};
                    archive(value);
                    payload = destruct{std::move(value)};
                }
                break;
            }
            case 2: {
                if constexpr(is_pageless<T, entity_type>) {
                    ENTTX_ASSERT(false, "update_change encountered for pageless type during deserialization");
                    return *this;
                } else {
                    using update_change = typename change_type::update;
                    T old_value{}, new_value{};
                    archive(old_value, new_value);
                    payload = update_change{std::move(old_value), std::move(new_value)};
                }
                break;
            }
            default:
                ENTTX_ASSERT(false, "Unknown change payload index during deserialization");
                return *this;
            }

            changes.push_back(change_type{entity, std::move(payload)});
        }

        commit->template append_segment<T>(std::move(changes), storage_id);

        return *this;
    }

    /*!
    * @brief Deserializes the changes for a specific component type, using the default entity handler that returns the entity as-is.
    * @tparam T Component type.
    * @tparam Archive Archive type.
    * @param archive Archive to deserialize from.
    * @param storage_id Optional storage identifier.
    * @return This loader.
     */
    template<typename T, typename Archive>
    const basic_commit_loader& get(Archive& archive, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        return get<T>(archive, [](Archive& archive) {
            entity_type entity{};
            archive(entity);
            return entity;
        }, storage_id);
    }

private:
    commit_type* commit;
};

template<typename Commit>
basic_commit_loader(Commit& commit) -> basic_commit_loader<Commit>;

namespace internal 
{

template<typename Storage>
concept is_change_observer_storage = requires (Storage& s) {
    typename Storage::value_type;
    typename Storage::entity_type;
    { s.on_construct() };
    { s.on_destroy() };
}
&& (
    is_pageless<typename Storage::value_type, typename Storage::entity_type>
    ||
    requires (Storage& s) {
        { s.on_pre_update() };
        { s.on_update() };
    }
);

/*! @brief Tracks changes to components of type `T` within the given `Registry`. */
template<is_change_observer_storage Storage, typename Allocator = std::allocator<change<typename Storage::element_type, typename Storage::entity_type>>>
class basic_observer final: public basic_observer_base<typename Storage::registry_type> {
public:
    /*! @brief Storage for the component type being observed. */
    using storage_type = Storage;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename storage_type::entity_type;
    /*! @brief Type of the component being observed. */
    using element_type = typename storage_type::element_type;
    /*! @brief Allocator type for the changes vector. */
    using allocator_type = Allocator;
    /*! @brief Type of the change being tracked. */
    using change_type = change<element_type, entity_type>;
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
        if constexpr(!is_pageless<element_type, entity_type>) {
            storage.on_pre_update().template connect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template connect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template connect<&basic_observer::on_destroy>(*this);
    }

    void disconnect() override {
        storage.on_construct().template disconnect<&basic_observer::on_construct>(*this);
        if constexpr(!is_pageless<element_type, entity_type>) {
            storage.on_pre_update().template disconnect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template disconnect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template disconnect<&basic_observer::on_destroy>(*this);
    }

    void collect(commit_base_type& commit) override {
        // TODO: Add an option to flatten changes into a single net change for networking?
        if (!changes.empty()) {
            commit.template append_segment<element_type>(std::move(changes), storage_id);
            changes.clear();
        }
    }

private:
    // TODO: See comment in on_update 
    [[no_unique_address]]
    std::conditional_t<!is_pageless<element_type, entity_type>, 
        std::optional<element_type>, 
        std::monostate> pre_update_value;

    void on_construct(const entity_type entity) {
        if constexpr(is_pageless<element_type, entity_type>) {
            changes.emplace_back(entity, typename change_type::construct{});
        } else {
            changes.emplace_back(entity, typename change_type::construct{storage.get(entity)});
        }
    }

    void on_pre_update(const entity_type entity) requires(!is_pageless<element_type, entity_type>) {
        ENTTX_ASSERT(!pre_update_value.has_value(), "Pre-update value already set for entity");
        pre_update_value = storage.get(entity);
    }

    void on_update(const entity_type entity) requires(!is_pageless<element_type, entity_type>) {
        // TODO: Figure out if this is safe and the right way to do it.
        // Need to check if its legal that patch cannot modify the registry in a way that would invalidate this.
        changes.emplace_back(entity, typename change_type::update{std::move(pre_update_value).value(), storage.get(entity)});
        pre_update_value.reset();
    }

    void on_destroy(const entity_type entity) {
        if constexpr(is_pageless<element_type, entity_type>) {
            changes.emplace_back(entity, typename change_type::destruct{});
        } else {
            changes.emplace_back(entity, typename change_type::destruct{storage.get(entity)});
        }
    }
};
} // namespace internal

/*!
 * @brief Creates an observer for a specific component type.
 * @tparam T Component type.
 * @tparam Registry Registry type.
 * @param registry Registry to observe.
 * @param storage_id Optional storage identifier.
 * @return A unique pointer to the created observer.
 */
template<typename T, typename Registry>
[[nodiscard]] std::unique_ptr<basic_observer_base<Registry>> 
observe(Registry& registry, entt::id_type storage_id = entt::type_hash<T>::value()) {
    auto& storage = registry.template storage<T>(storage_id);
    using storage_type = std::remove_reference_t<decltype(storage)>;
    return std::make_unique<internal::basic_observer<storage_type>>(storage_id, storage);
}

} // namespace enttx