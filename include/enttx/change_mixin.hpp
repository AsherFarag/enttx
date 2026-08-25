/*!
 * @file change_mixin.hpp
 * @brief Mixin that adds change notification capabilities to a storage type.
 * 
 * @code
 * 
 * @endcode
 */

#pragma once
#include "config.hpp"

#include <entt/entity/mixin.hpp>
#include <entt/signal/sigh.hpp>

namespace enttx
{

namespace internal
{
template<typename, typename>
class basic_pre_update_mixin;
} // namespace internal

/*!
 * @brief Mixin that extends entt::basic_sigh_mixin to provide a pre-update signal for component updates.
 * @tparam Type Underlying storage type.
 * @tparam Registry Type of the registry that owns the storage.
 */
template<typename Type, typename Registry>
using basic_change_mixin = entt::basic_sigh_mixin<internal::basic_pre_update_mixin<Type, Registry>, Registry>;

/*!
 * @brief Alias declaration for the most common use case.
 * @tparam Type Underlying storage type.
 */
template<typename Type>
using change_mixin = basic_change_mixin<Type, entt::basic_registry<typename Type::entity_type, typename Type::base_type::allocator_type>>;

namespace internal
{

/*! @brief Checks if a type has an `on_pre_update` static member function. */
template<typename, typename>
struct has_on_pre_update final: std::false_type {};

template<typename Type, typename Registry>
requires std::invocable<decltype(&Type::on_pre_update), Registry &, typename Registry::entity_type>
struct has_on_pre_update<Type, Registry>: std::true_type {};

template<typename Type, typename Registry>
class basic_pre_update_mixin : public Type {
    using underlying_type = Type;
    using owner_type = Registry;

    using basic_registry_type = entt::basic_registry<typename owner_type::entity_type, typename owner_type::allocator_type>;
    using sigh_type = entt::sigh<void(owner_type &, const typename underlying_type::entity_type), typename underlying_type::allocator_type>;

    static_assert(std::is_base_of_v<basic_registry_type, owner_type>, "Invalid registry type");

    [[nodiscard]] auto &owner_or_assert() const noexcept {
        ENTTX_ASSERT(owner != nullptr, "Invalid pointer to registry");
        return static_cast<owner_type &>(*owner);
    }

protected:
    void bind_any(entt::any value) noexcept override {
        owner = entt::any_cast<basic_registry_type>(&value);

        if constexpr(!std::is_same_v<registry_type, basic_registry_type>) {
            if(owner == nullptr) {
                owner = entt::any_cast<registry_type>(&value);
            }
        }

        underlying_type::bind_any(std::move(value));
    }
public:
    /*! @brief Allocator type. */
    using allocator_type = underlying_type::allocator_type;
    /*! @brief Underlying entity identifier. */
    using entity_type = underlying_type::entity_type;
    /*! @brief Expected registry type. */
    using registry_type = owner_type;

    /*! @brief Default constructor. */
    basic_pre_update_mixin()
        : basic_pre_update_mixin{allocator_type{}} {}

    /**
     * @brief Constructs an empty storage with a given allocator.
     * @param allocator The allocator to use.
     */
    explicit basic_pre_update_mixin(const allocator_type &allocator)
        : underlying_type{allocator},
          owner{},
          pre_update{allocator} {
        if constexpr(has_on_pre_update<typename underlying_type::element_type, Registry>::value) {
            entt::sink{pre_update}.template connect<&underlying_type::element_type::on_pre_update>();
        }
    }

    /*! @brief Default copy constructor, deleted on purpose. */
    basic_pre_update_mixin(const basic_pre_update_mixin &) = delete;

    /**
     * @brief Move constructor.
     * @param other The instance to move from.
     */
    basic_pre_update_mixin(basic_pre_update_mixin &&other) noexcept
        : underlying_type{static_cast<underlying_type &&>(other)},
          owner{other.owner},
          pre_update{std::move(other.pre_update)} {}

    /**
     * @brief Allocator-extended move constructor.
     * @param other The instance to move from.
     * @param allocator The allocator to use.
     */
    basic_pre_update_mixin(basic_pre_update_mixin &&other, const allocator_type &allocator)
        : underlying_type{static_cast<underlying_type &&>(other), allocator},
          owner{other.owner},
          pre_update{std::move(other.pre_update), allocator} {}

    /*! @brief Default destructor. */
    ~basic_pre_update_mixin() override = default;

    /**
     * @brief Default copy assignment operator, deleted on purpose.
     * @return This mixin.
     */
    basic_pre_update_mixin &operator=(const basic_pre_update_mixin &) = delete;

    /**
     * @brief Move assignment operator.
     * @param other The instance to move from.
     * @return This mixin.
     */
    basic_pre_update_mixin &operator=(basic_pre_update_mixin &&other) noexcept {
        swap(other);
        return *this;
    }

    /**
     * @brief Exchanges the contents with those of a given storage.
     * @param other Storage to exchange the content with.
     */
    void swap(basic_pre_update_mixin &other) noexcept {
        using std::swap;
        swap(owner, other.owner);
        swap(pre_update, other.pre_update);
        underlying_type::swap(other);
    }
    /**
     * @brief Returns a sink object.
     *
     * The sink returned by this function can be used to receive notifications
     * just before an instance is explicitly updated.<br/>
     * Listeners are invoked just before the object has been updated.
     *
     * @sa sink
     *
     * @return A temporary sink object.
     */
    [[nodiscard]] auto on_pre_update() noexcept {
        return entt::sink{pre_update};
    }

    /**
     * @brief Updates the instance assigned to a given entity in-place.
     * @tparam Func Types of the function objects to invoke.
     * @param entt A valid identifier.
     * @param func Valid function objects.
     */
    template<typename... Func>
    void patch(const entity_type entt, Func &&...func) {
        pre_update.publish(owner_or_assert(), entt);
        underlying_type::patch(entt, std::forward<Func>(func)...);
    }

private:
    basic_registry_type *owner;
    sigh_type pre_update;
};

} // namespace internal

} // namespace enttx