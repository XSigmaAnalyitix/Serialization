#pragma once

#include <utility>
#include <string_view>
#include <type_traits>

namespace serialization {

// Helper to iterate over integer sequences
template <typename T, T... S, typename F>
constexpr void for_sequence(std::integer_sequence<T, S...>, F&& f) {
    (f(std::integral_constant<T, S>{}), ...);
}

// Reflection for class members
template <typename Class, typename T>
class reflection_impl {
public:
    using class_type = Class;
    using member_type = T;
    using pointer_type = T Class::*;
    
    constexpr reflection_impl(pointer_type member, 
                             std::string_view name, 
                             std::string_view description = "") noexcept
        : member_(member), name_(name), description_(description) {}
    
    constexpr pointer_type member() const noexcept { return member_; }
    constexpr std::string_view name() const noexcept { return name_; }
    constexpr std::string_view description() const noexcept { return description_; }
    
    constexpr const T& get(const Class& obj) const noexcept {
        return obj.*member_;
    }
    
    constexpr T& get(Class& obj) const noexcept {
        return obj.*member_;
    }
    
    constexpr void set(Class& obj, const T& value) const {
        obj.*member_ = value;
    }
    
private:
    pointer_type member_;
    std::string_view name_;
    std::string_view description_;
};

// Factory function
template <typename Class, typename T>
constexpr auto reflection(T Class::* member, 
                         std::string_view name, 
                         std::string_view description = "") noexcept {
    return reflection_impl<Class, T>{member, name, description};
}

// For empty classes
template <typename Class>
class reflection_empty {
public:
    using class_type = Class;
    
    constexpr reflection_empty(std::string_view name = "") noexcept 
        : name_(name) {}
    
    constexpr std::string_view name() const noexcept { return name_; }
    
private:
    std::string_view name_;
};

template <typename Class>
constexpr auto reflection_no_member(std::string_view name = "") noexcept {
    return reflection_empty<Class>{name};
}

} // namespace serialization