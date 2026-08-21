#pragma once

#include <entt/entity/entity.hpp>

#include <iostream>
#include <string>
#include <sstream>

template<typename T, typename Archive>
concept has_serialize = requires(Archive& ar, T& value) {
    { ::serialize(ar, value) } -> std::same_as<void>;
};


/*! @brief Simple string output archive based on `Cereal` */
struct output_archive {
    std::ostringstream stream;

    template<typename T>
    requires requires(const T& value) { stream << value; }
    void operator()(const T& value) {
        stream << value << ' ';
    }

    template<typename T>
    requires has_serialize<T, output_archive>
    void operator()(const T& value) {
        ::serialize(*this, const_cast<T&>(value));
    }

    template<typename T, typename... Args>
    void operator()(const T& value, Args&&... args) {
        (*this)(value);
        (*this)(std::forward<Args>(args)...);
    }

    void operator()(const std::uint8_t value) {
        // Serialize std::uint8_t as a uint32_t to avoid issues with char types in streams
        stream << static_cast<std::uint32_t>(value) << ' ';
    }

    void operator()(const entt::entity value) {
        stream << entt::to_integral(value) << ' ';
    }
};

/*! @brief Simple string input archive based on `Cereal` */
struct input_archive {
    std::istringstream stream;

    input_archive(const std::string& str) : stream{str} {}

    template<typename T>
    requires requires(T& value) { stream >> value; }
    void operator()(T& value) {
        stream >> value;
    }

    template<typename T>
    requires has_serialize<T, input_archive>
    void operator()(T& value) {
        ::serialize(*this, value);
    }

    template<typename T, typename... Args>
    void operator()(T& value, Args&&... args) {
        (*this)(value);
        (*this)(std::forward<Args>(args)...);
    }

    void operator()(std::uint8_t& value) {
        // Serialize std::uint8_t as a uint32_t to avoid issues with char types in streams
        std::uint32_t temp;
        stream >> temp;
        value = static_cast<std::uint8_t>(temp);
    }

    void operator()(entt::entity& value) {
        std::uint32_t temp;
        stream >> temp;
        value = entt::entity{temp};
    }
};