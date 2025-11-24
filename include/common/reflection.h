#pragma once

#ifndef __SERIALIZATION_WRAP__

namespace serialization
{
#define TYPENAME(T) #T

template <typename T, T... S, typename F>
constexpr void for_sequence(std::integer_sequence<T, S...> /*unused*/, F&& f)
{
    using unpack_t = int[];  // NOLINT
    (void)unpack_t{(static_cast<void>(f(std::integral_constant<T, S>{})), 0)..., 0};
}

template <typename Class, typename T>
struct reflection_impl
{
public:
    constexpr reflection_impl(T Class::* member, const char* name, const char* description)
        : member_(member), name_(name), description_(description)
    {
    }

    using type = T;

    auto member() const { return member_; }
    auto name() const { return name_; }
    auto description() const { return description_; }

    auto member() { return member_; }
    auto name() { return name_; }
    auto description() { return description_; }

private:
    T Class::*  member_;
    const char* name_;
    const char* description_;
};

template <typename Class, typename T>
constexpr auto reflection(
    T Class::* member, const char* name, const char* description = TYPENAME(T))
{
    return reflection_impl<Class, T>{member, name, description};
}

// New reflection_empty for classes without members
template <typename Class>
struct reflection_empty
{
public:
    constexpr reflection_empty(const char* name) : name_(name) {}

    auto name() const { return name_; }
    auto name() { return name_; }

private:
    const char* name_;
};

// New reflection helper for classes without members
template <typename Class>
constexpr auto reflection_no_member(const char* name = "")
{
    return reflection_empty<Class>{name};
}
}  // namespace serialization
#endif
