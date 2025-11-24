#pragma once

#ifndef __SERIALIZATION_WRAP__

#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "common/export.h"
#include "common/serialization_type_traits.h"
#include "util/multi_process_stream.h"
#include "util/registry.h"
#include "util/string_util.h"

#include <nlohmann/json.hpp>

#define SERIALIZATION_LOG_WARNING(x) std::cerr << "Warning: " << x << std::endl;

namespace serialization
{
class key;
class tenor;
class datetime;
}  // namespace serialization

namespace serialization
{
using json = nlohmann::ordered_json;

using json_serilaization_function_t = std::function<void(json&, void*, bool)>;
using binary_serilaization_function_t =
    std::function<void(serialization::multi_process_stream&, void*, bool)>;

SERIALIZATION_API SERIALIZATION_DECLARE_FUNCTION_REGISTRY(
    JsonSerializationRegistry, json_serilaization_function_t);
SERIALIZATION_API SERIALIZATION_DECLARE_FUNCTION_REGISTRY(
    BinarySerializationRegistry, binary_serilaization_function_t);

//-----------------------------------------------------------------------------
template <typename archiver>
class archiver_wrapper
{
};

//-----------------------------------------------------------------------------
template <typename EnumType>
void to_json(json& archive, const EnumType& e)
{
    static_assert(std::is_enum_v<EnumType>, "Type must be an enum");
    archive = std::string(enum_to_string(e));
}

//-----------------------------------------------------------------------------
template <typename EnumType>
void from_json(const json& archive, EnumType& e)
{
    static_assert(std::is_enum_v<EnumType>, "Type must be an enum");
    if (archive.is_string())
    {
        auto str = archive.get<std::string>();
        e        = string_to_enum<EnumType>(str);
    }
    else
    {
        e = static_cast<EnumType>(archive.get<int>());
    }
}

//-----------------------------------------------------------------------------
inline constexpr std::string_view CLASS_NAME{R"(Class)"};
inline constexpr std::string_view SIZE_NAME{R"(Size)"};

template <>
struct archiver_wrapper<json>
{
    template <typename T, std::enable_if_t<is_base_serializable<T>::value, int> = 0>
    static void push(json& archive, const T& obj)
    {
        if constexpr (std::is_same<T, serialization::datetime>::value)
        {
            archive = static_cast<double>(obj);
        }
        else if constexpr (std::is_same<T, const char*>::value)
        {
            auto x  = std::string{obj};
            archive = x;
        }
        else if constexpr (std::is_enum<T>::value)
        {
            to_json(archive, obj);
        }
        else if constexpr (
            std::is_same<T, serialization::tenor>::value || std::is_same<T, serialization::key>::value)
        {
            archive = obj.to_string();
        }
        else
        {
            archive = obj;
        }
    };

    template <typename T, std::enable_if_t<is_base_serializable<T>::value, int> = 0>
    static void pop(json& archive, T& obj)
    {
        if constexpr (std::is_same<T, serialization::datetime>::value)
        {
            obj = archive.get<double>();
        }
        else if constexpr (std::is_same<T, const char*>::value)
        {
            obj = archive.get<std::string>().c_str();
        }
        else if constexpr (std::is_enum<T>::value)
        {
            from_json(archive, obj);
        }
        else if constexpr (
            std::is_same<T, serialization::tenor>::value || std::is_same<T, serialization::key>::value)
        {
            obj = archive.get<std::string>();
        }
        else
        {
            obj = archive.get<T>();
        }
    };

    static void push_class_name(json& archive, const std::string& name)
    {
        archive[CLASS_NAME.data()] = name;
    };

    static auto pop_class_name(json& archive)
    {
        // Check if the key exists in the JSON object
        if (!archive.contains(CLASS_NAME.data()))
        {
            SERIALIZATION_LOG_WARNING("json does not have a class name field!");
        }
        else
        {
            const auto& name = archive[CLASS_NAME.data()];
            // Check if the value is a string
            if (name.is_string())
            {
                return name.get<std::string>();
            }
            else
            {
                SERIALIZATION_LOG_WARNING("name" << name << "is not a string!");
            }
        }

        // Return an empty string if the key doesn't exist or the value isn't a string
        return std::string();
    };

    static void push_index(json& archive, std::string_view index_name, unsigned int idx)
    {
        archive[index_name.data()] = idx;
    };

    static auto pop_index(json& archive, std::string_view index_name)
    {
        return archive[index_name.data()].get<unsigned int>();
    };

    static const auto& get(const json& archive, std::string_view idx)
    {
        return archive[idx.data()];
    };

    static const auto& get(const json& archive, size_t idx) { return archive[idx]; };

    static auto& get(json& archive, std::string_view idx) { return archive[idx.data()]; };

    static auto& get(json& archive, size_t idx) { return archive[idx]; };

    static void resize(json& /*archive*/, size_t /*size*/) { ; };

    static auto size(json& archive) { return archive.size(); };

    static auto registery() { return serialization::JsonSerializationRegistry(); }
};

//-----------------------------------------------------------------------------
template <>
struct archiver_wrapper<serialization::multi_process_stream>
{
    template <typename T, std::enable_if_t<is_base_serializable<T>::value, int> = 0>
    static void push(serialization::multi_process_stream& archive, const T& obj)
    {
        if constexpr (serialization::has_float<T>::value)
            archive << static_cast<double>(obj.as_float());
        else if constexpr (std::is_enum<T>::value)
            archive << static_cast<int>(obj);
        else if constexpr (
            std::is_same<T, serialization::tenor>::value || std::is_same<T, serialization::key>::value)
            archive << obj.to_string();
        else
            archive << obj;
    };

    template <typename T, std::enable_if_t<is_base_serializable<T>::value, int> = 0>
    static void pop(serialization::multi_process_stream& archive, T& obj)
    {
        if constexpr (serialization::has_float<T>::value)
        {
            double d;
            archive >> d;
            obj = d;
        }
        else if constexpr (std::is_enum<T>::value)
        {
            int i = 0;
            archive >> i;
            obj = static_cast<T>(i);
        }
        else if constexpr (
            std::is_same<T, serialization::tenor>::value || std::is_same<T, serialization::key>::value)
        {
            std::string s;
            archive >> s;
            obj = s;
        }
        else
            archive >> obj;
    };

    static void push_class_name(serialization::multi_process_stream& archive, const std::string& name)
    {
        archive << name;
    };

    static auto pop_class_name(serialization::multi_process_stream& archive)
    {
        std::string ret;
        archive >> ret;
        return ret;
    };

    static void push_index(
        serialization::multi_process_stream& archive, std::string_view /*index_name*/, unsigned int idx)
    {
        archive << idx;
    };

    static auto pop_index(serialization::multi_process_stream& archive, std::string_view /*index_name*/)
    {
        unsigned int idx;
        archive >> idx;
        return idx;
    };

    static const auto& get(const serialization::multi_process_stream& archive, std::string_view /*idx*/)
    {
        return archive;
    };

    static auto& get(serialization::multi_process_stream& archive, std::string_view /*idx*/)
    {
        return archive;
    };

    static auto& get(serialization::multi_process_stream& archive, size_t /*idx*/) { return archive; };

    static void resize(serialization::multi_process_stream& archive, size_t n)
    {
        archive << static_cast<unsigned int>(n);
    };

    static auto size(serialization::multi_process_stream& archive)
    {
        unsigned int n;
        archive >> n;
        return static_cast<size_t>(n);
    };

    static auto registery() { return serialization::BinarySerializationRegistry(); }
};

}  // namespace serialization

#endif
