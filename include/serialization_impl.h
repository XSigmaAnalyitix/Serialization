#pragma once

#include <array>
#include <cstddef>
#include <exception>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>

#include "common/archiver_wrapper.h"
#include "common/helper.h"
#include "common/reflection.h"
#include "common/serialization_type_traits.h"
#include "util/pointer.h"
#include "util/registry.h"
#include "util/string_util.h"

//-----------------------------------------------------------------------------
// Error handling macros
//-----------------------------------------------------------------------------
namespace serialization::detail
{
template <typename... Args>
[[noreturn]] inline void throw_error(Args&&... args)
{
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    throw std::runtime_error(oss.str());
}
}  // namespace serialization::detail

#define SERIALIZATION_THROW(...) ::serialization::detail::throw_error(__VA_ARGS__)

#define SERIALIZATION_CHECK(condition, ...)                                        \
    do                                                                             \
    {                                                                              \
        if (!(condition))                                                          \
        {                                                                          \
            SERIALIZATION_THROW("Check failed: ", #condition, " - ", __VA_ARGS__); \
        }                                                                          \
    } while (false)

#define CONCATENATE_IMPL(a, b) a##b
#define CONCATENATE(a, b) CONCATENATE_IMPL(a, b)

//-----------------------------------------------------------------------------
namespace serialization
{

//-----------------------------------------------------------------------------
// Helper type trait to detect reflection_empty type
//-----------------------------------------------------------------------------
template <typename T>
struct is_reflection_empty : std::false_type
{
};

template <typename Class>
struct is_reflection_empty<reflection_empty<Class>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_reflection_empty_v = is_reflection_empty<T>::value;

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
inline constexpr std::string_view VALUE_NAME = "Value";
inline constexpr std::string_view INDEX_NAME = "Index";
inline constexpr std::string_view EMPTY_NAME = "null object!";

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
template <typename Archiver, typename T>
void serialization_save(Archiver& archive, const T& obj);

template <typename Archiver, typename T>
void serialization_load(Archiver& archive, T& obj);

//-----------------------------------------------------------------------------
// Registry registration helper
//-----------------------------------------------------------------------------
template <typename Archiver, typename T>
void register_serializer_impl(Archiver& archive, void* obj, bool load_obj)
{
    if (load_obj)
    {
        auto* obj_ptr       = static_cast<ptr_const<T>*>(obj);
        auto  loaded_object = serialization::access::serializer::make_ptr<T>();
        serialization::serialization_load(archive, *loaded_object);
        obj_ptr->reset(loaded_object.release());
    }
    else
    {
        const auto* obj_ptr = static_cast<const T*>(obj);
        serialization::serialization_save(archive, *obj_ptr);
    }
}

//-----------------------------------------------------------------------------
namespace impl
{

//-----------------------------------------------------------------------------
// Container serialization - Sequential containers
//-----------------------------------------------------------------------------
template <typename Archiver, typename Container>
void load_container(Archiver& archive, Container& container)
{
    const size_t size = archiver_wrapper<Archiver>::size(archive);

    container.clear();

    if constexpr (has_reserve<Container>::value)
    {
        container.reserve(size);
    }

    for (size_t i = 0; i < size; ++i)
    {
        if constexpr (has_emplace_back<Container>::value)
        {
            container.emplace_back();
            serialization::serialization_load(
                archiver_wrapper<Archiver>::get(archive, i), container.back());
        }
        else
        {
            typename Container::value_type item;
            serialization::serialization_load(archiver_wrapper<Archiver>::get(archive, i), item);
            container.insert(container.end(), std::move(item));
        }
    }
}

template <typename Archiver, typename Container>
void save_container(Archiver& archive, const Container& container)
{
    const size_t size = container.size();
    archiver_wrapper<Archiver>::resize(archive, size);

    if constexpr (has_random_access<Container>::value)
    {
        for (size_t i = 0; i < size; ++i)
        {
            serialization::serialization_save(
                archiver_wrapper<Archiver>::get(archive, i), container[i]);
        }
    }
    else
    {
        size_t i = 0;
        for (const auto& item : container)
        {
            serialization::serialization_save(archiver_wrapper<Archiver>::get(archive, i), item);
            ++i;
        }
    }
}

//-----------------------------------------------------------------------------
// Associative container serialization
//-----------------------------------------------------------------------------
template <typename Archiver, typename Container>
void load_associative_container(Archiver& archive, Container& container)
{
    const size_t size = archiver_wrapper<Archiver>::size(archive);

    container.clear();

    if constexpr (has_reserve<Container>::value)
    {
        container.reserve(size);
    }

    if constexpr (is_map_like_v<Container>)
    {
        SERIALIZATION_CHECK(
            size % 2 == 0, "Invalid map serialization: odd number of elements (", size, ")");

        for (size_t i = 0; i < size / 2; ++i)
        {
            typename Container::key_type    key;
            typename Container::mapped_type value;

            serialization::serialization_load(archiver_wrapper<Archiver>::get(archive, 2 * i), key);
            serialization::serialization_load(
                archiver_wrapper<Archiver>::get(archive, 2 * i + 1), value);

            container.emplace(std::move(key), std::move(value));
        }
    }
    else
    {
        for (size_t i = 0; i < size; ++i)
        {
            typename Container::value_type value;
            serialization::serialization_load(archiver_wrapper<Archiver>::get(archive, i), value);
            container.emplace(std::move(value));
        }
    }
}

template <typename Archiver, typename Container>
void save_associative_container(Archiver& archive, const Container& container)
{
    const size_t size = container.size();

    if constexpr (has_mapped_type<Container>::value)
    {
        archiver_wrapper<Archiver>::resize(archive, 2 * size);
    }
    else
    {
        archiver_wrapper<Archiver>::resize(archive, size);
    }

    size_t i = 0;
    for (const auto& item : container)
    {
        if constexpr (has_mapped_type<Container>::value)
        {
            serialization::serialization_save(
                archiver_wrapper<Archiver>::get(archive, i++), item.first);
            serialization::serialization_save(
                archiver_wrapper<Archiver>::get(archive, i++), item.second);
        }
        else
        {
            serialization::serialization_save(archiver_wrapper<Archiver>::get(archive, i++), item);
        }
    }
}

//-----------------------------------------------------------------------------
// Generic serializer implementation
//-----------------------------------------------------------------------------
template <typename Archiver, typename T>
struct serializer_impl
{
    //-------------------------------------------------------------------------
    // Save object with reflection
    //-------------------------------------------------------------------------
    static void save_object(Archiver& archive, const T* obj)
    {
        if (obj == nullptr)
        {
            archiver_wrapper<Archiver>::push_class_name(archive, std::string(EMPTY_NAME));
            return;
        }

        constexpr auto nbProperties =
            std::tuple_size<decltype(serialization::access::serializer::tuple<T>())>::value;

        std::string class_name = demangle(typeid(*obj).name());
        archiver_wrapper<Archiver>::push_class_name(archive, class_name);

        if constexpr (nbProperties > 0)
        {
            for_sequence(
                std::make_index_sequence<nbProperties>{},
                [&](auto i)
                {
                    constexpr auto property =
                        std::get<i>(serialization::access::serializer::tuple<T>());
                    const auto& name        = property.name();
                    auto&       archive_tmp = archiver_wrapper<Archiver>::get(archive, name);

                    if constexpr (!is_reflection_empty_v<std::decay_t<decltype(property)>>)
                    {
                        const auto& member_ref = obj->*(property.member());
                        serialization::serialization_save(archive_tmp, member_ref);
                    }
                });
        }
    }

    //-------------------------------------------------------------------------
    // Load object with reflection
    //-------------------------------------------------------------------------
    static void load_object(Archiver& archive, T& obj)
    {
        constexpr auto nbProperties =
            std::tuple_size<decltype(serialization::access::serializer::tuple<T>())>::value;

        if constexpr (nbProperties > 0)
        {
            const auto class_name = archiver_wrapper<Archiver>::pop_class_name(archive);

            SERIALIZATION_CHECK(!class_name.empty(), "Invalid class name");

            if (class_name != EMPTY_NAME)
            {
                for_sequence(
                    std::make_index_sequence<nbProperties>{},
                    [&](auto i)
                    {
                        constexpr auto property =
                            std::get<i>(serialization::access::serializer::tuple<T>());
                        const auto& name        = property.name();
                        auto&       archive_tmp = archiver_wrapper<Archiver>::get(archive, name);

                        if constexpr (!is_reflection_empty_v<std::decay_t<decltype(property)>>)
                        {
                            using member_type =
                                typename std::decay_t<decltype(property)>::member_type;
                            auto& member_ref = obj.*(property.member());
                            serialization::serialization_load<Archiver, member_type>(
                                archive_tmp, member_ref);
                        }
                    });

                serialization::access::serializer::initialize(obj);
            }
        }
    }

    //-------------------------------------------------------------------------
    // Main save dispatcher
    //-------------------------------------------------------------------------
    static void serialization_save(Archiver& archive, const T& obj)
    {
        if constexpr (is_base_serializable<T>::value)
        {
            archiver_wrapper<Archiver>::push(archive, obj);
        }
        else if constexpr (is_container<T>::value)
        {
            if constexpr (is_associative_container<T>::value)
            {
                save_associative_container(archive, obj);
            }
            else
            {
                save_container(archive, obj);
            }
        }
        else if constexpr (std::is_pointer<T>::value)
        {
            using U = std::remove_pointer_t<T>;
            if constexpr (has_reflection<U>::value)
            {
                serializer_impl<Archiver, U>::save_object(archive, obj);
            }
            else
            {
                static_assert(
                    always_false<T>::value, "Pointer type without reflection not supported");
            }
        }
        else if constexpr (has_reflection<T>::value)
        {
            save_object(archive, &obj);
        }
        else
        {
            static_assert(always_false<T>::value, "Type not supported for serialization");
        }
    }

    //-------------------------------------------------------------------------
    // Main load dispatcher
    //-------------------------------------------------------------------------
    static void serialization_load(Archiver& archive, T& obj)
    {
        if constexpr (is_base_serializable<T>::value)
        {
            archiver_wrapper<Archiver>::pop(archive, obj);
        }
        else if constexpr (has_reflection<T>::value)
        {
            load_object(archive, obj);
        }
        else if constexpr (is_container<T>::value)
        {
            if constexpr (is_associative_container<T>::value)
            {
                load_associative_container(archive, obj);
            }
            else
            {
                load_container(archive, obj);
            }
        }
        else
        {
            static_assert(always_false<T>::value, "Type not supported for deserialization");
        }
    }
};

//-----------------------------------------------------------------------------
// std::array specialization
//-----------------------------------------------------------------------------
template <typename Archiver, typename Item, std::size_t Size>
struct serializer_impl<Archiver, std::array<Item, Size>>
{
    static void serialization_load(Archiver& archive, std::array<Item, Size>& array)
    {
        const auto archive_size = archiver_wrapper<Archiver>::size(archive);
        SERIALIZATION_CHECK(
            archive_size == Size,
            "Array size mismatch: expected ",
            Size,
            " but got ",
            archive_size);

        for (size_t i = 0; i < Size; ++i)
        {
            serialization::serialization_load(
                archiver_wrapper<Archiver>::get(archive, i), array[i]);
        }
    }

    static void serialization_save(Archiver& archive, const std::array<Item, Size>& array)
    {
        archiver_wrapper<Archiver>::resize(archive, Size);

        for (size_t i = 0; i < Size; ++i)
        {
            serialization::serialization_save(
                archiver_wrapper<Archiver>::get(archive, i), array[i]);
        }
    }
};

//-----------------------------------------------------------------------------
// std::variant specialization
//-----------------------------------------------------------------------------
template <typename Archiver, typename... Types>
struct serializer_impl<Archiver, std::variant<Types...>>
{
    static void serialization_save(Archiver& archive, const std::variant<Types...>& variant)
    {
        static_assert(sizeof...(Types) <= 255, "Variant can contain at most 255 types");

        const auto variant_index = variant.index();

        SERIALIZATION_CHECK(
            variant_index != std::variant_npos, "Cannot serialize a valueless variant");

        const auto index = static_cast<unsigned char>(variant_index);

        std::visit(
            [&archive, index](const auto& object)
            {
                archiver_wrapper<Archiver>::push_index(archive, INDEX_NAME, index);
                serialization::serialization_save(
                    archiver_wrapper<Archiver>::get(archive, VALUE_NAME), object);
            },
            variant);
    }

    static void serialization_load(Archiver& archive, std::variant<Types...>& variant)
    {
        constexpr size_t num_types = sizeof...(Types);
        static_assert(num_types <= 255, "Variant can contain at most 255 types");

        const auto index = archiver_wrapper<Archiver>::pop_index(archive, INDEX_NAME);

        SERIALIZATION_CHECK(
            index < num_types, "Variant index ", index, " out of range (max ", num_types - 1, ")");

        using variant_type = std::variant<Types...>;
        using loader_type  = void (*)(Archiver& archive, variant_type& variant);

        static constexpr loader_type loaders[] = {
            [](Archiver& archive, variant_type& variant)
            {
                if constexpr (std::is_default_constructible_v<Types>)
                {
                    if (!std::holds_alternative<Types>(variant))
                    {
                        variant = Types{};
                    }
                    serialization::serialization_load(archive, std::get<Types>(variant));
                }
                else
                {
                    // For non-default constructible types
                    alignas(Types) unsigned char storage[sizeof(Types)];
                    auto*                        ptr =
                        access::serializer::placement_new<Types>(reinterpret_cast<void*>(storage));

                    try
                    {
                        serialization::serialization_load(archive, *ptr);
                        variant = std::move(*ptr);
                    }
                    catch (...)
                    {
                        access::serializer::destruct(*ptr);
                        throw;
                    }
                    access::serializer::destruct(*ptr);
                }
            }...};

        loaders[index](archiver_wrapper<Archiver>::get(archive, VALUE_NAME), variant);
    }
};

//-----------------------------------------------------------------------------
// std::pair specialization
//-----------------------------------------------------------------------------
template <typename Archiver, typename First, typename Second>
struct serializer_impl<Archiver, std::pair<First, Second>>
{
    static void serialization_load(Archiver& archive, std::pair<First, Second>& pair)
    {
        serialization::serialization_load(archiver_wrapper<Archiver>::get(archive, 0), pair.first);
        serialization::serialization_load(archiver_wrapper<Archiver>::get(archive, 1), pair.second);
    }

    static void serialization_save(Archiver& archive, const std::pair<First, Second>& pair)
    {
        serialization::serialization_save(archiver_wrapper<Archiver>::get(archive, 0), pair.first);
        serialization::serialization_save(archiver_wrapper<Archiver>::get(archive, 1), pair.second);
    }
};

//-----------------------------------------------------------------------------
// std::unique_ptr specializations
//-----------------------------------------------------------------------------
template <typename Archiver, typename Type>
struct serializer_impl<Archiver, ptr_unique_mutable<Type>>
{
    static void serialization_save(Archiver& archive, const ptr_unique_mutable<Type>& object)
    {
        SERIALIZATION_CHECK(object != nullptr, "Cannot serialize null unique_ptr");
        serialization::serialization_save(archive, *object);
    }

    static void serialization_load(Archiver& archive, ptr_unique_mutable<Type>& object)
    {
        auto loaded_object = serialization::access::serializer::make_ptr<Type>();
        serialization::serialization_load(archive, *loaded_object);
        object.reset(loaded_object.release());
    }
};

template <typename Archiver, typename Type>
struct serializer_impl<Archiver, ptr_unique_const<Type>>
{
    static void serialization_save(Archiver& archive, const ptr_unique_const<Type>& object)
    {
        SERIALIZATION_CHECK(object != nullptr, "Cannot serialize null unique_ptr");
        serialization::serialization_save(archive, *object);
    }

    static void serialization_load(Archiver& archive, ptr_unique_const<Type>& object)
    {
        auto loaded_object = serialization::access::serializer::make_ptr<Type>();
        serialization::serialization_load(archive, *loaded_object);
        object.reset(loaded_object.release());
    }
};

//-----------------------------------------------------------------------------
// Custom pointer type specializations
//-----------------------------------------------------------------------------
template <typename Archiver, typename Type>
struct serializer_impl<Archiver, ptr_mutable<Type>>
{
    using archiver_type = std::remove_cv_t<Archiver>;

    static void serialization_save(Archiver& archive, const ptr_mutable<Type>& object)
    {
        if (!object)
        {
            archiver_wrapper<Archiver>::push_class_name(archive, std::string(EMPTY_NAME));
            return;
        }

        if constexpr (has_reflection<Type>::value)
        {
            serialization::serialization_save(archive, object.get());
        }
        else
        {
            const auto* obj_ptr    = object.get();
            auto        class_name = demangle(typeid(*obj_ptr).name());

            archiver_wrapper<Archiver>::push_class_name(archive, class_name);

            if (archiver_wrapper<archiver_type>::registery()->Has(class_name))
            {
                // Note: Casting away const here - ensure registry handles this properly
                auto* mutable_ptr = const_cast<Type*>(obj_ptr);
                archiver_wrapper<archiver_type>::registery()->run(
                    class_name, archive, mutable_ptr, false);
            }
        }
    }

    static void serialization_load(Archiver& archive, ptr_mutable<Type>& object)
    {
        auto loaded_object = serialization::access::serializer::make_ptr<Type>();
        serialization::serialization_load(archive, *loaded_object);
        object.reset(loaded_object.release());
    }
};

template <typename Archiver, typename Type>
struct serializer_impl<Archiver, ptr_const<Type>>
{
    using archiver_type = std::remove_cv_t<Archiver>;

    static void serialization_save(Archiver& archive, const ptr_const<Type>& object)
    {
        if (!object)
        {
            archiver_wrapper<Archiver>::push_class_name(archive, std::string(EMPTY_NAME));
            return;
        }

        if constexpr (has_reflection<Type>::value)
        {
            serialization::serialization_save(archive, object.get());
        }
        else
        {
            const auto* obj_ptr    = object.get();
            auto        class_name = demangle(typeid(*obj_ptr).name());

            archiver_wrapper<Archiver>::push_class_name(archive, class_name);

            if (archiver_wrapper<archiver_type>::registery()->Has(class_name))
            {
                // Note: Casting away const - registry must handle const correctness
                auto* mutable_ptr = const_cast<Type*>(obj_ptr);
                archiver_wrapper<archiver_type>::registery()->run(
                    class_name, archive, mutable_ptr, false);
            }
        }
    }

    static void serialization_load(Archiver& archive, ptr_const<Type>& object)
    {
        const std::string class_name = archiver_wrapper<archiver_type>::pop_class_name(archive);

        if (class_name == EMPTY_NAME)
        {
            object = nullptr;
            return;
        }

        if (archiver_wrapper<archiver_type>::registery()->Has(class_name))
        {
            archiver_wrapper<archiver_type>::registery()->run(class_name, archive, &object, true);
            return;
        }

        if constexpr (has_reflection<Type>::value)
        {
            auto loaded_object = serialization::access::serializer::make_ptr<Type>();
            serialization::serialization_load(archive, *loaded_object);
            object.reset(loaded_object.release());
        }
        else
        {
            SERIALIZATION_THROW(
                "Cannot deserialize type '",
                class_name,
                "': not registered and no reflection available");
        }
    }
};

//-----------------------------------------------------------------------------
// std::tuple specialization
//-----------------------------------------------------------------------------
template <typename Archiver, typename... Ts>
struct serializer_impl<Archiver, std::tuple<Ts...>>
{
    static void serialization_load(Archiver& archive, std::tuple<Ts...>& tuple)
    {
        const auto archive_size = archiver_wrapper<Archiver>::size(archive);
        SERIALIZATION_CHECK(
            archive_size == sizeof...(Ts),
            "Tuple size mismatch: expected ",
            sizeof...(Ts),
            " but got ",
            archive_size);

        load_tuple_impl(archive, tuple, std::index_sequence_for<Ts...>{});
    }

    static void serialization_save(Archiver& archive, const std::tuple<Ts...>& tuple)
    {
        archiver_wrapper<Archiver>::resize(archive, sizeof...(Ts));
        save_tuple_impl(archive, tuple, std::index_sequence_for<Ts...>{});
    }

private:
    template <std::size_t... Is>
    static void load_tuple_impl(
        Archiver& archive, std::tuple<Ts...>& tuple, std::index_sequence<Is...>)
    {
        (serialization::serialization_load(
             archiver_wrapper<Archiver>::get(archive, Is), std::get<Is>(tuple)),
         ...);
    }

    template <std::size_t... Is>
    static void save_tuple_impl(
        Archiver& archive, const std::tuple<Ts...>& tuple, std::index_sequence<Is...>)
    {
        (serialization::serialization_save(
             archiver_wrapper<Archiver>::get(archive, Is), std::get<Is>(tuple)),
         ...);
    }
};

}  // namespace impl

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------
template <typename Archiver, typename T>
void serialization_save(Archiver& archive, const T& obj)
{
    impl::serializer_impl<Archiver, T>::serialization_save(archive, obj);
}

template <typename Archiver, typename T>
void serialization_load(Archiver& archive, T& obj)
{
    impl::serializer_impl<Archiver, T>::serialization_load(archive, obj);
}

}  // namespace serialization