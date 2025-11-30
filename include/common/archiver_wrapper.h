/* Copyright 2018 The Serialization Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "common/serialization_type_traits.h"
#include "util/export.h"
#include "util/multi_process_stream.h"
#include "util/registry.h"
#include "util/string_util.h"

//=============================================================================
// Logging Macros
//=============================================================================

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

//=============================================================================
// Serialization Function Type Aliases
//=============================================================================
// Note: These type aliases define the function signatures for serialization
// callbacks used in the registry pattern for polymorphic type handling.

/// @brief Function type for JSON serialization callbacks
/// @param archive The JSON object to serialize to/from
/// @param obj Pointer to the object being serialized
/// @param is_saving True if saving, false if loading
using json_serialization_function_t = std::function<void(json&, void*, bool)>;

/// @brief Function type for binary serialization callbacks
/// @param archive The binary stream to serialize to/from
/// @param obj Pointer to the object being serialized
/// @param is_saving True if saving, false if loading
using binary_serialization_function_t =
    std::function<void(serialization::multi_process_stream&, void*, bool)>;

SERIALIZATION_API SERIALIZATION_DECLARE_FUNCTION_REGISTRY(
    JsonSerializationRegistry, json_serialization_function_t);
SERIALIZATION_API SERIALIZATION_DECLARE_FUNCTION_REGISTRY(
    BinarySerializationRegistry, binary_serialization_function_t);

//=============================================================================
// Primary Template (Specialization Required)
//=============================================================================
// Note: The Archiver concept is defined in serialization_concepts.h

/// @brief Base template for archiver wrapper
/// Specializations must be provided for specific archiver types (json, multi_process_stream)
template <typename ArchiveType>
class archiver_wrapper
{
    // Primary template is intentionally empty
    // Specializations must be provided for concrete archiver types
};

//=============================================================================
// Enum Serialization Helpers
//=============================================================================

/// @brief Convert enum to JSON representation
/// @tparam EnumType Must be an enum type
/// @param archive The JSON object to write to
/// @param e The enum value to serialize
template <typename EnumType>
    requires std::is_enum_v<EnumType>
void to_json(json& archive, const EnumType& e)
{
    archive = std::string(enum_to_string(e));
}

/// @brief Convert JSON representation to enum
/// @tparam EnumType Must be an enum type
/// @param archive The JSON object to read from
/// @param e The enum value to deserialize into
template <typename EnumType>
    requires std::is_enum_v<EnumType>
void from_json(const json& archive, EnumType& e)
{
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

//=============================================================================
// Archive Field Names (Compile-time Constants)
//=============================================================================

/// @brief JSON field name for class type information
inline constexpr std::string_view CLASS_NAME{R"(Class)"};

/// @brief JSON field name for container size information
inline constexpr std::string_view SIZE_NAME{R"(Size)"};

//=============================================================================
// JSON Archiver Specialization
//=============================================================================

/// @brief Specialization of archiver_wrapper for JSON archives
/// Provides serialization/deserialization operations for JSON format
template <>
struct archiver_wrapper<json>
{
    /// @brief Serialize a base-serializable type to JSON
    /// @tparam T Must satisfy is_base_serializable concept
    /// @param archive The JSON object to write to
    /// @param obj The object to serialize
    /// @note const char* is supported for serialization but use std::string for deserialization
    template <typename T>
        requires is_base_serializable<T>::value
    static void push(json& archive, const T& obj)
    {
        if constexpr (std::is_same_v<T, serialization::datetime>)
        {
            archive = static_cast<double>(obj);
        }
        else if constexpr (std::is_same_v<T, const char*>)
        {
            // Note: const char* can be serialized (converted to string)
            // but cannot be deserialized. Use std::string for round-trip serialization.
            if (obj == nullptr)
            {
                archive = nullptr;
            }
            else
            {
                archive = std::string{obj};
            }
        }
        else if constexpr (std::is_same_v<T, std::monostate>)
        {
            archive = nullptr;
        }
        else if constexpr (std::is_enum_v<T>)
        {
            to_json(archive, obj);
        }
        else if constexpr (
            std::is_same_v<T, serialization::tenor> || std::is_same_v<T, serialization::key>)
        {
            archive = obj.to_string();
        }
        else
        {
            archive = obj;
        }
    }

    /// @brief Deserialize a base-serializable type from JSON
    /// @tparam T Must satisfy is_base_serializable concept
    /// @param archive The JSON object to read from
    /// @param obj The object to deserialize into
    template <typename T>
        requires is_base_serializable<T>::value
    static void pop(json& archive, T& obj)
    {
        if constexpr (std::is_same_v<T, serialization::datetime>)
        {
            obj = archive.get<double>();
        }
        else if constexpr (std::is_same_v<T, const char*>)
        {
            // Note: const char* cannot be safely deserialized
            // This should never be instantiated - use std::string instead
            static_assert(
                serialization::always_false<T>::value,
                "Cannot deserialize const char* - use std::string instead");
        }
        else if constexpr (std::is_same_v<T, std::monostate>)
        {
            obj = std::monostate{};
        }
        else if constexpr (std::is_enum_v<T>)
        {
            from_json(archive, obj);
        }
        else if constexpr (
            std::is_same_v<T, serialization::tenor> || std::is_same_v<T, serialization::key>)
        {
            obj = archive.get<std::string>();
        }
        else
        {
            obj = archive.get<T>();
        }
    }

    /// @brief Store class type information in JSON
    /// @param archive The JSON object to write to
    /// @param name The class name to store
    static void push_class_name(json& archive, const std::string& name)
    {
        archive[std::string(CLASS_NAME)] = name;
    }

    /// @brief Retrieve class type information from JSON
    /// @param archive The JSON object to read from
    /// @return The stored class name, or empty string if not found
    [[nodiscard]] static auto pop_class_name(json& archive)
    {
        if (!archive.contains(std::string(CLASS_NAME)))
        {
            SERIALIZATION_LOG_WARNING("json does not have a class name field!");
            return std::string();
        }

        const auto& name = archive[std::string(CLASS_NAME)];
        if (name.is_string())
        {
            return name.get<std::string>();
        }

        SERIALIZATION_LOG_WARNING("Class name field is not a string!");
        return std::string();
    }

    /// @brief Store container index in JSON
    /// @param archive The JSON object to write to
    /// @param index_name The field name for the index
    /// @param idx The index value to store
    static void push_index(json& archive, std::string_view index_name, unsigned int idx)
    {
        archive[std::string(index_name)] = idx;
    }

    /// @brief Retrieve container index from JSON
    /// @param archive The JSON object to read from
    /// @param index_name The field name for the index
    /// @return The stored index value
    [[nodiscard]] static auto pop_index(json& archive, std::string_view index_name)
    {
        return archive[std::string(index_name)].get<unsigned int>();
    }

    /// @brief Get JSON element by string key (const)
    /// @param archive The JSON object to read from
    /// @param idx The key to access
    /// @return Const reference to the JSON element
    [[nodiscard]] static const auto& get(const json& archive, std::string_view idx)
    {
        return archive[std::string(idx)];
    }

    /// @brief Get JSON element by numeric index (const)
    /// @param archive The JSON object to read from
    /// @param idx The index to access
    /// @return Const reference to the JSON element
    [[nodiscard]] static const auto& get(const json& archive, size_t idx) { return archive[idx]; }

    /// @brief Get JSON element by string key (mutable)
    /// @param archive The JSON object to modify
    /// @param idx The key to access
    /// @return Mutable reference to the JSON element
    static auto& get(json& archive, std::string_view idx) { return archive[std::string(idx)]; }

    /// @brief Get JSON element by numeric index (mutable)
    /// @param archive The JSON object to modify
    /// @param idx The index to access
    /// @return Mutable reference to the JSON element
    static auto& get(json& archive, size_t idx) { return archive[idx]; }

    /// @brief Resize JSON array (no-op for JSON objects)
    /// @param archive The JSON object (unused)
    /// @param size The desired size (unused)
    static void resize([[maybe_unused]] json& archive, [[maybe_unused]] size_t size)
    {
        // JSON arrays are dynamically sized; no explicit resize needed
    }

    /// @brief Get the size of a JSON array or object
    /// @param archive The JSON object to query
    /// @return The number of elements
    [[nodiscard]] static auto size(const json& archive) { return archive.size(); }

    /// @brief Get the JSON serialization registry
    /// @return Pointer to the global JSON serialization registry
    [[nodiscard]] static auto registry() { return serialization::JsonSerializationRegistry(); }
};

//=============================================================================
// Binary Stream Archiver Specialization
//=============================================================================

/// @brief Specialization of archiver_wrapper for binary stream archives
/// Provides serialization/deserialization operations for binary format
template <>
struct archiver_wrapper<serialization::multi_process_stream>
{
    /// @brief Serialize a base-serializable type to binary stream
    /// @tparam T Must satisfy is_base_serializable concept
    /// @param archive The binary stream to write to
    /// @param obj The object to serialize
    template <typename T>
        requires is_base_serializable<T>::value
    static void push(serialization::multi_process_stream& archive, const T& obj)
    {
        if constexpr (std::is_same_v<T, std::monostate>)
        {
            // monostate is an empty type - write a marker byte
            archive << static_cast<unsigned char>(0);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            archive << static_cast<int>(obj);
        }
        else if constexpr (
            std::is_same_v<T, serialization::tenor> || std::is_same_v<T, serialization::key>)
        {
            archive << obj.to_string();
        }
        else
        {
            archive << obj;
        }
    }

    /// @brief Deserialize a base-serializable type from binary stream
    /// @tparam T Must satisfy is_base_serializable concept
    /// @param archive The binary stream to read from
    /// @param obj The object to deserialize into
    template <typename T>
        requires is_base_serializable<T>::value
    static void pop(serialization::multi_process_stream& archive, T& obj)
    {
        if constexpr (std::is_same_v<T, std::monostate>)
        {
            // monostate is an empty type - read and discard the marker byte
            unsigned char marker;
            archive >> marker;
            obj = std::monostate{};
        }
        else if constexpr (std::is_enum_v<T>)
        {
            int i = 0;
            archive >> i;
            obj = static_cast<T>(i);
        }
        else if constexpr (
            std::is_same_v<T, serialization::tenor> || std::is_same_v<T, serialization::key>)
        {
            std::string s;
            archive >> s;
            obj = s;
        }
        else
        {
            archive >> obj;
        }
    }

    /// @brief Store class type information in binary stream
    /// @param archive The binary stream to write to
    /// @param name The class name to store
    static void push_class_name(
        serialization::multi_process_stream& archive, const std::string& name)
    {
        archive << name;
    }

    /// @brief Retrieve class type information from binary stream
    /// @param archive The binary stream to read from
    /// @return The stored class name
    [[nodiscard]] static auto pop_class_name(serialization::multi_process_stream& archive)
    {
        std::string ret;
        archive >> ret;
        return ret;
    }

    /// @brief Store container index in binary stream
    /// @param archive The binary stream to write to
    /// @param index_name Unused (for API compatibility with JSON archiver)
    /// @param idx The index value to store
    static void push_index(
        serialization::multi_process_stream& archive,
        [[maybe_unused]] std::string_view    index_name,
        unsigned int                         idx)
    {
        archive << idx;
    }

    /// @brief Retrieve container index from binary stream
    /// @param archive The binary stream to read from
    /// @param index_name Unused (for API compatibility with JSON archiver)
    /// @return The stored index value
    [[nodiscard]] static auto pop_index(
        serialization::multi_process_stream& archive, [[maybe_unused]] std::string_view index_name)
    {
        unsigned int idx;
        archive >> idx;
        return idx;
    }

    /// @brief Get binary stream reference by string key (const)
    /// @param archive The binary stream to read from
    /// @param idx Unused (for API compatibility with JSON archiver)
    /// @return Const reference to the binary stream
    [[nodiscard]] static const auto& get(
        const serialization::multi_process_stream& archive, [[maybe_unused]] std::string_view idx)
    {
        return archive;
    }

    /// @brief Get binary stream reference by string key (mutable)
    /// @param archive The binary stream to modify
    /// @param idx Unused (for API compatibility with JSON archiver)
    /// @return Mutable reference to the binary stream
    static auto& get(
        serialization::multi_process_stream& archive, [[maybe_unused]] std::string_view idx)
    {
        return archive;
    }

    /// @brief Get binary stream reference by numeric index (mutable)
    /// @param archive The binary stream to modify
    /// @param idx Unused (for API compatibility with JSON archiver)
    /// @return Mutable reference to the binary stream
    static auto& get(serialization::multi_process_stream& archive, [[maybe_unused]] size_t idx)
    {
        return archive;
    }

    /// @brief Write container size to binary stream
    /// @param archive The binary stream to write to
    /// @param n The size value to store
    static void resize(serialization::multi_process_stream& archive, size_t n)
    {
        archive << static_cast<unsigned int>(n);
    }

    /// @brief Read container size from binary stream
    /// @param archive The binary stream to read from
    /// @return The stored size value
    [[nodiscard]] static auto size(serialization::multi_process_stream& archive)
    {
        unsigned int n;
        archive >> n;
        return static_cast<size_t>(n);
    }

    /// @brief Get the binary serialization registry
    /// @return Pointer to the global binary serialization registry
    [[nodiscard]] static auto registry() { return serialization::BinarySerializationRegistry(); }
};

}  // namespace serialization