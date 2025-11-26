#pragma once

/**
 * @file compile_time_helpers.h
 * @brief C++20 consteval and constexpr utilities for compile-time reflection and metadata
 * @requires C++20 or later
 */

static_assert(__cplusplus >= 202002L, "This header requires C++20 or later");

#include <array>
#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace serialization
{

//-----------------------------------------------------------------------------
// Compile-time string utilities
//-----------------------------------------------------------------------------

/**
 * @brief Compile-time fixed string for template parameters
 */
template <std::size_t N>
struct fixed_string
{
    char data[N + 1]{};

    consteval fixed_string(const char (&str)[N + 1])
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            data[i] = str[i];
        }
        data[N] = '\0';
    }

    [[nodiscard]] consteval std::size_t size() const { return N; }

    [[nodiscard]] consteval std::string_view view() const { return std::string_view(data, N); }

    [[nodiscard]] consteval const char* c_str() const { return data; }

    [[nodiscard]] consteval char operator[](std::size_t i) const { return data[i]; }

    // Comparison operators
    template <std::size_t M>
    [[nodiscard]] consteval bool operator==(const fixed_string<M>& other) const
    {
        if (N != M)
            return false;
        for (std::size_t i = 0; i < N; ++i)
        {
            if (data[i] != other.data[i])
                return false;
        }
        return true;
    }

    template <std::size_t M>
    [[nodiscard]] consteval bool operator!=(const fixed_string<M>& other) const
    {
        return !(*this == other);
    }
};

// Deduction guide
template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N - 1>;

//-----------------------------------------------------------------------------
// Compile-time type name extraction
//-----------------------------------------------------------------------------

namespace detail
{
template <typename T>
consteval std::string_view type_name_impl()
{
#if defined(__clang__)
    std::string_view name = __PRETTY_FUNCTION__;
    std::size_t      prefix_len =
        std::string_view("std::string_view serialization::detail::type_name_impl() [T = ").size();
    std::size_t suffix_len = 1;
#elif defined(__GNUC__)
    std::string_view name = __PRETTY_FUNCTION__;
    std::size_t      prefix_len =
        std::string_view(
            "consteval std::string_view serialization::detail::type_name_impl() [with T = ")
            .size();
    std::size_t suffix_len = 1;
#elif defined(_MSC_VER)
    std::string_view name       = __FUNCSIG__;
    std::size_t      prefix_len = std::string_view(
                                 "class std::basic_string_view<char,struct std::char_traits<char> "
                                      "> __cdecl serialization::detail::type_name_impl<")
                                 .size();
    std::size_t suffix_len = std::string_view(">(void)").size();
#else
    return "unknown";
#endif
    name.remove_prefix(prefix_len);
    name.remove_suffix(suffix_len);
    return name;
}
}  // namespace detail

/**
 * @brief Get compile-time type name
 */
template <typename T>
consteval std::string_view type_name()
{
    return detail::type_name_impl<T>();
}

//-----------------------------------------------------------------------------
// Compile-time reflection metadata
//-----------------------------------------------------------------------------

/**
 * @brief Compile-time member count detector
 */
namespace detail
{
// Helper to detect number of members using structured bindings
template <typename T, std::size_t... Is>
consteval bool can_bind_with(std::index_sequence<Is...>)
{
    return requires(T t) { [](auto&&... args) {}(Is..., t); };
}

template <typename T, std::size_t N>
consteval bool has_n_members()
{
    if constexpr (std::is_aggregate_v<T>)
    {
        return can_bind_with<T>(std::make_index_sequence<N>{});
    }
    else
    {
        return false;
    }
}

template <typename T, std::size_t N = 0>
consteval std::size_t count_members_impl()
{
    if constexpr (has_n_members<T, N>())
    {
        return count_members_impl<T, N + 1>();
    }
    else
    {
        return N;
    }
}
}  // namespace detail

/**
 * @brief Get compile-time member count for aggregate types
 */
template <typename T>
consteval std::size_t member_count()
{
    return detail::count_members_impl<T>();
}

//-----------------------------------------------------------------------------
// Compile-time property metadata
//-----------------------------------------------------------------------------

/**
 * @brief Compile-time property descriptor
 */
template <typename Class, typename MemberType, auto MemberPtr>
struct property_descriptor
{
    using class_type  = Class;
    using member_type = MemberType;

    static constexpr auto member_pointer = MemberPtr;

    [[nodiscard]] static consteval std::string_view name()
    {
        // This would require compiler magic or reflection TS
        // For now, return a placeholder
        return "property";
    }

    [[nodiscard]] static consteval std::string_view type_name_str()
    {
        return type_name<MemberType>();
    }

    [[nodiscard]] static consteval bool is_const() { return std::is_const_v<MemberType>; }

    [[nodiscard]] static consteval bool is_reference() { return std::is_reference_v<MemberType>; }

    [[nodiscard]] static consteval bool is_pointer() { return std::is_pointer_v<MemberType>; }
};

//-----------------------------------------------------------------------------
// Compile-time tuple utilities
//-----------------------------------------------------------------------------

/**
 * @brief Get compile-time tuple size
 */
template <typename Tuple>
consteval std::size_t tuple_size()
{
    return std::tuple_size_v<Tuple>;
}

/**
 * @brief Check if type is tuple-like at compile time
 */
template <typename T>
consteval bool is_tuple_like()
{
    return requires {
        typename std::tuple_size<T>::type;
        requires std::derived_from<
            std::tuple_size<T>,
            std::integral_constant<std::size_t, std::tuple_size_v<T>>>;
    };
}

//-----------------------------------------------------------------------------
// Compile-time type list
//-----------------------------------------------------------------------------

template <typename... Ts>
struct type_list
{
    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I>
    using type_at = std::tuple_element_t<I, std::tuple<Ts...>>;

    template <typename T>
    static consteval bool contains()
    {
        return (std::is_same_v<T, Ts> || ...);
    }

    template <typename T>
    static consteval std::size_t index_of()
    {
        std::size_t index = 0;
        bool        found = false;
        ((std::is_same_v<T, Ts> ? (found = true, false) : (found ? false : (++index, false))) ||
         ...);
        return found ? index : size;
    }

    template <template <typename> class Predicate>
    static consteval std::size_t count_if()
    {
        return (Predicate<Ts>::value + ...);
    }

    template <template <typename> class F>
    static constexpr void for_each(auto&& func)
    {
        (func.template operator()<Ts>(), ...);
    }
};

//-----------------------------------------------------------------------------
// Compile-time hash
//-----------------------------------------------------------------------------

/**
 * @brief FNV-1a hash at compile time
 */
consteval std::size_t fnv1a_hash(std::string_view str)
{
    constexpr std::size_t prime  = sizeof(std::size_t) == 8 ? 0x100000001b3ULL : 0x01000193UL;
    constexpr std::size_t offset = sizeof(std::size_t) == 8 ? 0xcbf29ce484222325ULL : 0x811c9dc5UL;

    std::size_t hash = offset;
    for (char c : str)
    {
        hash ^= static_cast<std::size_t>(c);
        hash *= prime;
    }
    return hash;
}

/**
 * @brief Type name hash at compile time
 */
template <typename T>
consteval std::size_t type_hash()
{
    return fnv1a_hash(type_name<T>());
}

//-----------------------------------------------------------------------------
// Compile-time array utilities
//-----------------------------------------------------------------------------

/**
 * @brief Create compile-time array from parameter pack
 */
template <typename T, T... Values>
consteval auto make_array()
{
    return std::array<T, sizeof...(Values)>{Values...};
}

/**
 * @brief Create compile-time array from function
 */
template <typename T, std::size_t N, typename F>
consteval auto make_array_from_function(F&& f)
{
    std::array<T, N> arr{};
    for (std::size_t i = 0; i < N; ++i)
    {
        arr[i] = f(i);
    }
    return arr;
}

//-----------------------------------------------------------------------------
// Compile-time validation
//-----------------------------------------------------------------------------

/**
 * @brief Validate that all types in a pack satisfy a predicate
 */
template <template <typename> class Predicate, typename... Ts>
consteval bool all_of()
{
    return (Predicate<Ts>::value && ...);
}

/**
 * @brief Validate that any type in a pack satisfies a predicate
 */
template <template <typename> class Predicate, typename... Ts>
consteval bool any_of()
{
    return (Predicate<Ts>::value || ...);
}

/**
 * @brief Validate that no type in a pack satisfies a predicate
 */
template <template <typename> class Predicate, typename... Ts>
consteval bool none_of()
{
    return !(Predicate<Ts>::value || ...);
}

//-----------------------------------------------------------------------------
// Compile-time size calculations
//-----------------------------------------------------------------------------

/**
 * @brief Calculate total size of all types in a pack
 */
template <typename... Ts>
consteval std::size_t total_size()
{
    return (sizeof(Ts) + ...);
}

/**
 * @brief Calculate max size of all types in a pack
 */
template <typename... Ts>
consteval std::size_t max_size()
{
    std::size_t max = 0;
    ((max = sizeof(Ts) > max ? sizeof(Ts) : max), ...);
    return max;
}

/**
 * @brief Calculate max alignment of all types in a pack
 */
template <typename... Ts>
consteval std::size_t max_alignment()
{
    std::size_t max = 0;
    ((max = alignof(Ts) > max ? alignof(Ts) : max), ...);
    return max;
}

//-----------------------------------------------------------------------------
// Compile-time bit manipulation
//-----------------------------------------------------------------------------

/**
 * @brief Count set bits at compile time
 */
consteval std::size_t popcount(std::size_t value)
{
    std::size_t count = 0;
    while (value)
    {
        count += value & 1;
        value >>= 1;
    }
    return count;
}

/**
 * @brief Check if value is power of 2 at compile time
 */
consteval bool is_power_of_two(std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

/**
 * @brief Round up to next power of 2 at compile time
 */
consteval std::size_t next_power_of_two(std::size_t value)
{
    if (value == 0)
        return 1;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    if constexpr (sizeof(std::size_t) == 8)
    {
        value |= value >> 32;
    }
    return value + 1;
}

}  // namespace serialization
