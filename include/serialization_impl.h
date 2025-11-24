#pragma once

#ifndef __SERIALIZATION_WRAP__

#include <array>
#include <cstddef>
#include <iterator>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>

#include "common/archiver_wrapper.h"
#include "common/helper.h"
#include "common/pointer.h"
#include "common/reflection.h"
#include "common/serialization_type_traits.h"
#include "util/registry.h"
#include "util/string_util.h"

#define CONCATENATE(a, b) a + b
#define SERIALIZATION_THROW(...)
#define SERIALIZATION_CHECK(condition, ...)

namespace serialization
{  // Helper type trait to detect reflection type
template <typename T>
struct is_reflection_empty : std::false_type
{
};

template <typename Class>
struct is_reflection_empty<reflection_empty<Class>> : std::true_type
{
};

inline constexpr std::string_view VALUE_NAME{R"(Value)"};
inline constexpr std::string_view INDEX_NAME{R"(Index)"};

//-----------------------------------------------------------------------------
template <typename archiver, typename T>
static void serialization_save(archiver& archive, const T& obj);

template <typename archiver, typename T>
static void serialization_load(archiver& archive, T& obj);

//-----------------------------------------------------------------------------
template <typename archiver, typename T>
void register_serilizer_impl(archiver& archive, void* obj, bool load_obj)
{
    if (load_obj)
    {
        auto loaded_object = serialization::access::serilizer::make_ptr<T>();
        serialization::serialization_load<archiver, T>(archive, *loaded_object);
        auto obj_t = (ptr_const<T>*)obj;
        obj_t->reset(loaded_object.release());
    }
    else
    {
        auto obj_t = (const T*)(obj);
        serialization::serialization_save<archiver, T>(archive, *obj_t);
    }
};

//-----------------------------------------------------------------------------

namespace impl
{
inline static std::string EMPTY_NAME = "null object!";

//-----------------------------------------------------------------------------
// Serialize resizable containers, operates on loading (input) archives.
template <typename archiver, typename Container>
static void load_container(archiver& archive, Container& container)
{
    try
    {
        const size_t size = archiver_wrapper<archiver>::size(archive);

        if constexpr (serialization::has_reserve<Container>::value)
        {
            container.reserve(size);
        }

        container.clear();

        for (size_t i = 0; i < size; ++i)
        {
            typename Container::value_type item;
            serialization::serialization_load(archiver_wrapper<archiver>::get(archive, i), item);
            container.insert(container.end(), std::move(item));
        }
    }
    catch (const std::exception& e)
    {
        SERIALIZATION_THROW("Error loading container : ", e.what());
    }
};

template <typename archiver, typename Container>
static void save_container(archiver& archive, const Container& container)
{
    try
    {
        const size_t size = container.size();
        archiver_wrapper<archiver>::resize(archive, size);

        if constexpr (serialization::has_random_access<Container>::value)
        {
            for (size_t i = 0; i < size; ++i)
            {
                serialization::serialization_save(archiver_wrapper<archiver>::get(archive, i), container[i]);
            }
        }
        else
        {
            size_t i = 0;
            for (const auto& item : container)
            {
                serialization::serialization_save(archiver_wrapper<archiver>::get(archive, i), item);
                ++i;
            }
        }
    }
    catch (const std::exception& e)
    {
        SERIALIZATION_THROW("Error saving container: ", e.what());
    }
}

//-----------------------------------------------------------------------------
template <typename archiver, typename Container>
static void load_associative_container(archiver& archive, Container& container)
{
    try
    {
        const size_t size = archiver_wrapper<archiver>::size(archive);

        container.clear();
        if constexpr (serialization::has_reserve<Container>::value)
        {
            container.reserve(size);
        }

        if constexpr (serialization::is_map_like_v<Container>)
        {
            for (size_t i = 0; i < size / 2; ++i)
            {
                // This is a map-like container
                typename Container::key_type    key;
                typename Container::mapped_type value;

                serialization::serialization_load(archiver_wrapper<archiver>::get(archive, 2 * i), key);
                serialization::serialization_load(archiver_wrapper<archiver>::get(archive, 2 * i + 1), value);

                container.emplace(std::move(key), std::move(value));
            }
        }
        else
        {
            for (size_t i = 0; i < size; ++i)
            {
                // This is a set-like container
                typename Container::value_type value;

                serialization::serialization_load(archiver_wrapper<archiver>::get(archive, i), value);

                container.emplace(std::move(value));
            }
        }
    }
    catch (const std::exception& e)
    {
        SERIALIZATION_THROW("Error loading associative container: {}", e.what());
    }
}

template <typename archiver, typename Container>
static void save_associative_container(archiver& archive, const Container& container)
{
    try
    {
        const size_t size = container.size();
        if constexpr (serialization::has_mapped_type<Container>::value)
        {
            archiver_wrapper<archiver>::resize(archive, 2 * size);
        }
        else
        {
            archiver_wrapper<archiver>::resize(archive, size);
        }
        size_t i = 0;
        for (const auto& item : container)
        {
            if constexpr (serialization::has_mapped_type<Container>::value)
            {
                // This is a map-like container
                serialization::serialization_save(archiver_wrapper<archiver>::get(archive, i++), item.first);
                serialization::serialization_save(archiver_wrapper<archiver>::get(archive, i++), item.second);
            }
            else
            {
                // This is a set-like container
                serialization::serialization_save(archiver_wrapper<archiver>::get(archive, i++), item);
            }
        }
    }
    catch (const std::exception& e)
    {
        SERIALIZATION_THROW("Error saving associative container: ", e.what());
    }
}

//-----------------------------------------------------------------------------
template <typename archiver, typename T>
struct serilizer_impl
{
    static void save_object(archiver& archive, const T* obj)
    {
        if (obj == nullptr)
        {
            archiver_wrapper<archiver>::push_class_name(archive, EMPTY_NAME);
            return;
        }
        try
        {
            constexpr auto nbProperties =
                std::tuple_size<decltype(serialization::access::serilizer::tuple<T>())>::value;

            std::string s = demangle(typeid(*obj).name());
            archiver_wrapper<archiver>::push_class_name(archive, s);

            if constexpr (nbProperties > 0)
            {
                for_sequence(
                    std::make_index_sequence<nbProperties>{},
                    [&](auto i)
                    {
                        constexpr auto p    = std::get<i>(serialization::access::serilizer::tuple<T>());
                        const auto&    name = p.name();
                        auto&          archive_tmp = archiver_wrapper<archiver>::get(archive, name);
                        if constexpr (!is_reflection_empty<std::decay_t<decltype(p)>>::value)
                        {
                            const auto& ptr = obj->*(p.member());
                            serialization::serialization_save(archive_tmp, ptr);
                        }
                    });
            }
        }
        catch (const std::exception& e)
        {
            SERIALIZATION_THROW("Error saving object of type ", typeid(*obj).name(), " ", e.what());
        }
    }

    static void load_object(archiver& archive, T& obj)
    {
        try
        {
            constexpr auto n =
                std::tuple_size<decltype(serialization::access::serilizer::tuple<T>())>::value;

            if constexpr (n > 0)
            {
                const auto s = archiver_wrapper<archiver>::pop_class_name(archive);

                //SERIALIZATION_CHECK(!s.empty(), "Non valid class name");

                if (s != EMPTY_NAME)
                {
                    for_sequence(
                        std::make_index_sequence<n>{},
                        [&](auto i)
                        {
                            constexpr auto p = std::get<i>(serialization::access::serilizer::tuple<T>());

                            const auto& name  = p.name();
                            auto& archive_tmp = archiver_wrapper<archiver>::get(archive, name);

                            if constexpr (!is_reflection_empty<std::decay_t<decltype(p)>>::value)
                            {
                                auto& ptr = obj.*(p.member());
                                serialization::serialization_load<archiver, typename decltype(p)::type>(
                                    archive_tmp, ptr);
                            }
                        });

                    serialization::access::serilizer::initialize(obj);
                }
            }
        }
        catch (const std::exception& e)
        {
            SERIALIZATION_THROW("Error loading object of type ", typeid(obj).name(), e.what());
        }
    }

    static void serialization_save(archiver& archive, const T& obj)
    {
        try
        {
            if constexpr (is_base_serializable<T>::value)
            {
                archiver_wrapper<archiver>::push(archive, obj);
            }
            else if constexpr (serialization::is_container<T>::value)
            {
                if constexpr (serialization::is_associative_container<T>::value)
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
                if constexpr (serialization::has_reflection<U>::value)
                {
                    serilizer_impl<archiver, U>::save_object(archive, obj);
                }
            }
            else if constexpr (serialization::has_reflection<T>::value)
            {
                save_object(archive, &obj);
            }
            else
            {
                static_assert(always_false<T>::value, "Unsupported type for serialization");
            }
        }
        catch (const std::exception& e)
        {
            SERIALIZATION_THROW("Error saving object of type", typeid(obj).name(), e.what());
        }
    }

    static void serialization_load(archiver& archive, T& obj)
    {
        try
        {
            if constexpr (is_base_serializable<T>::value)
            {
                archiver_wrapper<archiver>::pop(archive, obj);
            }
            else if constexpr (serialization::has_reflection<T>::value)
            {
                load_object(archive, obj);
            }
            else if constexpr (serialization::is_container<T>::value)
            {
                if constexpr (serialization::is_associative_container<T>::value)
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
                static_assert(always_false<T>::value, "Unsupported type for deserialization");
            }
        }
        catch (const std::exception& e)
        {
            SERIALIZATION_THROW(
                "Error loading object of type: ", demangle(typeid(obj).name()), " - ", e.what());
        }
    }
};

//-----------------------------------------------------------------------------
/*Serialize std::array, operates on loading(input) archives.*/
template <typename archiver, typename Item, std::size_t size>
struct serilizer_impl<archiver, std::array<Item, size>>
{
    static void serialization_load(archiver& archive, std::array<Item, size>& array)
    {
        auto size2 = archiver_wrapper<archiver>::size(archive);
        //SERIALIZATION_CHECK(size2 == size);

        // Serialize every item.
        size_t i = 0;
        for (auto& item : array)
        {
            serialization::serialization_load(archiver_wrapper<archiver>::get(archive, i), item);
            i++;
        }
    }

    static void serialization_save(archiver& archive, const std::array<Item, size>& array)
    {
        archiver_wrapper<archiver>::resize(archive, array.size());

        size_t i = 0;
        // Serialize every item.
        for (auto& item : array)
        {
            serialization::serialization_save(archiver_wrapper<archiver>::get(archive, i), item);
            i++;
        }
    }
};

//-----------------------------------------------------------------------------
template <typename archiver, typename... Types>
struct serilizer_impl<archiver, std::variant<Types...>>
{
    static void serialization_save(archiver& archive, const std::variant<Types...>& variant)
    {
        // Test for maximum number of types.
        static_assert(sizeof...(Types) < 0xff, "Max variant types reached.");

        // The variant index.
        auto variant_index = variant.index();

        // Disallow serializations of valueless variant.
        //SERIALIZATION_CHECK(std::variant_npos != variant_index, "Cannot serialize a valueless variant.");

        // The index to serialization_save.
        auto index = static_cast<unsigned char>(variant_index & 0xff);

        // Save the variant object.
        std::visit(
            [index, &archive](auto& object)
            {
                archiver_wrapper<archiver>::push_index(archive, INDEX_NAME, index);
                serialization::serialization_save(archiver_wrapper<archiver>::get(archive, VALUE_NAME), object);
            },
            variant);
    }

    static void serialization_load(archiver& archive, std::variant<Types...>& variant)
    {
        constexpr size_t size = sizeof...(Types);

        static_assert(size < 0xff, "Max variant types reached.");

        auto index = archiver_wrapper<archiver>::pop_index(archive, INDEX_NAME);

        // Check that loaded index is inside bounds.
        //SERIALIZATION_CHECK(index < size, "Variant index out of range");

        // The variant type.
        using variant_type = std::variant<Types...>;

        // Loader type.
        using loader_type = void (*)(archiver& archive, variant_type& variant);

        // Loaders per variant index.
        static constexpr loader_type loaders[] = {
            [](auto& archive, auto& variant)
            {
                // If the type is default constructible.
                if constexpr (std::is_default_constructible_v<Types>)
                {
                    // If does not have the needed type, assign it.
                    if (!std::get_if<Types>(&variant))
                    {
                        variant = Types{};
                    }

                    // Load the value.
                    serialization::serialization_load(archive, *std::get_if<Types>(&variant));
                }
                else
                {
                    constexpr size_t len   = sizeof...(Types);
                    constexpr size_t align = alignof(Types);
                    // The object storage.
                    std::aligned_storage_t<len, align> storage;

                    // Create the object at the storage.
                    std::unique_ptr<Types, void (*)(Types*)> object(
                        access::serilizer::placement_new<Types...>(std::addressof(storage)),
                        [](auto pointer) { access::serilizer::destruct(*pointer); });

                    serialization::serialization_load(archive, *object);

                    // Assign the loaded object.
                    variant = std::move(*object);
                }
            }...};

        // Execute the appropriate loader.
        loaders[index](archiver_wrapper<archiver>::get(archive, VALUE_NAME), variant);
    }
};

//-----------------------------------------------------------------------------
// Serialize std::pair, operates on loading (input) archives.
template <typename archiver, typename First, typename Second>
struct serilizer_impl<archiver, std::pair<First, Second>>
{
    static void serialization_load(archiver& archive, std::pair<First, Second>& pair)
    {
        // Serialize first, then second.
        serialization::serialization_load(archiver_wrapper<archiver>::get(archive, 0), pair.first);
        serialization::serialization_load(archiver_wrapper<archiver>::get(archive, 1), pair.second);
    }

    static void serialization_save(archiver& archive, const std::pair<First, Second>& pair)
    {
        // Serialize first, then second.
        serialization::serialization_save(archiver_wrapper<archiver>::get(archive, 0), pair.first);
        serialization::serialization_save(archiver_wrapper<archiver>::get(archive, 1), pair.second);
    }
};

//-----------------------------------------------------------------------------
// Serialize std::unique_ptr of non polymorphic, in case of a loading
// (input) archive.
template <typename archiver, typename Type>
struct serilizer_impl<archiver, ptr_unique_mutable<Type>>
{
    static void serialization_save(archiver& archive, const ptr_unique_mutable<Type>& object)
    {
        // Serialize the object.
        serialization::serialization_save(archive, *object);
    }

    static void serialization_load(archiver& archive, ptr_unique_mutable<Type>& object)
    {
        // Construct a new object.
        auto loaded_object = serialization::access::serilizer::make_ptr<Type>();

        // Serialize the object.
        serialization::serialization_load(archive, *loaded_object);

        // Transfer the object.
        object.reset(loaded_object.release());
    }
};

//-----------------------------------------------------------------------------
template <typename archiver, typename Type>
struct serilizer_impl<archiver, ptr_unique_const<Type>>
{
    static void serialization_save(archiver& archive, const ptr_unique_const<Type>& object)
    {
        // Serialize the object.
        serialization::serialization_save(archive, *object);
    }

    static void serialization_load(archiver& archive, ptr_unique_const<Type>& object)
    {
        // Construct a new object.
        auto loaded_object = serialization::access::serilizer::make_ptr<Type>();

        // Serialize the object.
        serialization::serialization_load(archive, *loaded_object);

        // Transfer the object.
        object.reset(loaded_object.release());
    }
};

//-----------------------------------------------------------------------------
template <typename archiver, typename Type>
struct serilizer_impl<archiver, ptr_mutable<Type>>
{
    using type = typename std::remove_cv<archiver>::type;
    static void serialization_save(archiver& archive, const ptr_mutable<Type>& object)
    {
        // Serialize the object.
        if constexpr (serialization::has_reflection<Type>::value)
        {
            serialization::serialization_save(archive, object.get());
        }
        else
        {
            auto* obj_t = object.get();
            auto  name  = demangle(typeid(*obj_t).name());

            archiver_wrapper<archiver>::push_class_name(archive, name);

            if (archiver_wrapper<type>::registery()->Has(name))
            {
                archiver_wrapper<type>::registery()->run(name, archive, obj_t, false);
            }
        }
    }

    static void serialization_load(archiver& archive, ptr_mutable<Type>& object)
    {
        // Construct a new object.
        auto loaded_object = serialization::access::serilizer::make_ptr<Type>();

        // Serialize the object.
        serialization::serialization_load(archive, *loaded_object);

        // Transfer the object.
        object.reset(loaded_object.release());
    }
};

//-----------------------------------------------------------------------------
template <typename archiver, typename Type>
struct serilizer_impl<archiver, ptr_const<Type>>
{
    using type = typename std::remove_cv<archiver>::type;

    static void serialization_save(archiver& archive, const ptr_const<Type>& object)
    {
        // Serialize the object.
        if constexpr (serialization::has_reflection<Type>::value)
        {
            serialization::serialization_save(archive, object.get());
        }
        else
        {
            //fixme: if object is null?
            auto* obj_t = const_cast<Type*>(object.get());
            if (obj_t == nullptr)
            {
                archiver_wrapper<archiver>::push_class_name(archive, "null object!");
                return;
            }
            auto name = demangle(typeid(*obj_t).name());

            archiver_wrapper<archiver>::push_class_name(archive, name);

            if (archiver_wrapper<type>::registery()->Has(name))
            {
                archiver_wrapper<type>::registery()->run(name, archive, obj_t, false);
            }
        }
    }

    static void serialization_load(archiver& archive, ptr_const<Type>& object)
    {
        if constexpr (serialization::has_reflection<Type>::value)
        {
            auto loaded_object = serialization::access::serilizer::make_ptr<Type>();

            // Serialize the object.
            serialization::serialization_load(archive, *loaded_object);

            // Transfer the object.
            object.reset(loaded_object.release());
        }
        else
        {
            std::string name = archiver_wrapper<type>::pop_class_name(archive);
            if (name == EMPTY_NAME)
            {
                object = nullptr;
                return;
            }
            if (archiver_wrapper<type>::registery()->Has(name))
            {
                archiver_wrapper<type>::registery()->run(name, archive, &object, true);
            }
        }
    }
};

//-----------------------------------------------------------------------------
// Serializer implementation for std::tuple
template <typename Archiver, typename... Ts>
struct serilizer_impl<Archiver, std::tuple<Ts...>>
{
    template <std::size_t I>
    static void load_tuple_element(Archiver& archive, std::tuple<Ts...>& tuple)
    {
        if constexpr (I < sizeof...(Ts))
        {
            serialization::serialization_load(archiver_wrapper<Archiver>::get(archive, I), std::get<I>(tuple));
            load_tuple_element<I + 1>(archive, tuple);
        }
    }

    template <std::size_t I>
    static void save_tuple_element(Archiver& archive, const std::tuple<Ts...>& tuple)
    {
        if constexpr (I < sizeof...(Ts))
        {
            serialization::serialization_save(archiver_wrapper<Archiver>::get(archive, I), std::get<I>(tuple));
            save_tuple_element<I + 1>(archive, tuple);
        }
    }

    static void serialization_load(Archiver& archive, std::tuple<Ts...>& tuple)
    {
        try
        {
            auto size = archiver_wrapper<Archiver>::size(archive);
            //SERIALIZATION_CHECK(size == sizeof...(Ts), "Tuple size mismatch during load");
            load_tuple_element<0>(archive, tuple);
        }
        catch (const std::exception& e)
        {
            SERIALIZATION_THROW("Error loading tuple: ", e.what());
        }
    }

    static void serialization_save(Archiver& archive, const std::tuple<Ts...>& tuple)
    {
        try
        {
            archiver_wrapper<Archiver>::resize(archive, sizeof...(Ts));
            save_tuple_element<0>(archive, tuple);
        }
        catch (const std::exception& e)
        {
            SERIALIZATION_THROW("Error saving tuple: ", e.what());
        }
    }
};

//-----------------------------------------------------------------------------
}  // namespace impl

//-----------------------------------------------------------------------------
template <typename archiver, typename T>
static void serialization_save(archiver& archive, const T& obj)
{
    serialization::impl::serilizer_impl<archiver, T>::serialization_save(archive, obj);
};

//-----------------------------------------------------------------------------
template <typename archiver, typename T>
static void serialization_load(archiver& archive, T& obj)
{
    serialization::impl::serilizer_impl<archiver, T>::serialization_load(archive, obj);
};
}  // namespace serialization
#endif
