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


static_assert(__cplusplus >= 202002L, "This header requires C++20 or later");

#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "util/macros.h"

namespace serialization
{

//-----------------------------------------------------------------------------
// C++20 Concepts for Registry
//-----------------------------------------------------------------------------

template <typename K>
concept RegistryKey = std::equality_comparable<K> && std::is_copy_constructible_v<K>;

template <typename F>
concept RegistryFunction = std::is_invocable_v<F>;

//-----------------------------------------------------------------------------
// Enhanced thread-safe Registry with read-write locks
//-----------------------------------------------------------------------------

template <typename KeyType, typename Function>
class Registry
{
public:
    using key_type      = KeyType;
    using function_type = Function;

    Registry() = default;

    /**
     * @brief Register a function with a key
     * @param key The key to register
     * @param f The function to register
     * @throws std::invalid_argument if key already exists (optional behavior)
     */
    void Register(const KeyType& key, Function f)
    {
        std::unique_lock lock(mutex_);
        registry_[key] = std::move(f);
    }

    /**
     * @brief Register a function with move semantics
     */
    void Register(KeyType&& key, Function f)
    {
        std::unique_lock lock(mutex_);
        registry_[std::move(key)] = std::move(f);
    }

    /**
     * @brief Check if a key is registered (thread-safe)
     * @param key The key to check
     * @return true if the key is registered, false otherwise
     */
    [[nodiscard]] bool Has(const KeyType& key) const
    {
        std::shared_lock lock(mutex_);
        return registry_.contains(key);
    }

    /**
     * @brief Check if a key is registered with string_view (avoids string allocation)
     */
    [[nodiscard]] bool Has(std::string_view key) const
        requires std::same_as<KeyType, std::string>
    {
        std::shared_lock lock(mutex_);
        return registry_.contains(std::string(key));
    }

    /**
     * @brief Run a registered function with arguments
     * @throws std::out_of_range if key is not found
     */
    template <typename... Args>
    auto Run(const KeyType& key, Args&&... args) const
    {
        std::shared_lock lock(mutex_);
        auto             it = registry_.find(key);
        if (it == registry_.end())
        {
            throw std::out_of_range("Registry key not found: " + std::string(key));
        }
        return it->second(std::forward<Args>(args)...);
    }

    /**
     * @brief Run with non-const reference (for backward compatibility)
     */
    template <typename Arg1, typename Arg2, typename... Args>
    auto run(const KeyType& key, Arg1& arg1, Arg2* arg2, Args... args)
    {
        std::shared_lock lock(mutex_);
        auto             it = registry_.find(key);
        if (it == registry_.end())
        {
            throw std::out_of_range("Registry key not found");
        }
        return it->second(arg1, arg2, args...);
    }

    template <typename Arg1, typename Arg2, typename... Args>
    auto run(const KeyType& key, Arg1& arg1, Arg2& arg2, Args... args)
    {
        std::shared_lock lock(mutex_);
        auto             it = registry_.find(key);
        if (it == registry_.end())
        {
            throw std::out_of_range("Registry key not found");
        }
        return it->second(arg1, arg2, args...);
    }

    /**
     * @brief Get all registered keys
     * @return Vector of all keys currently registered
     */
    [[nodiscard]] std::vector<KeyType> Keys() const
    {
        std::shared_lock     lock(mutex_);
        std::vector<KeyType> keys;
        keys.reserve(registry_.size());

        for (const auto& [key, _] : registry_)
        {
            keys.push_back(key);
        }
        return keys;
    }

    /**
     * @brief Get the number of registered entries
     */
    [[nodiscard]] std::size_t Size() const
    {
        std::shared_lock lock(mutex_);
        return registry_.size();
    }

    /**
     * @brief Clear all registrations
     */
    void Clear()
    {
        std::unique_lock lock(mutex_);
        registry_.clear();
    }

    /**
     * @brief Unregister a key
     * @return true if the key was found and removed, false otherwise
     */
    bool Unregister(const KeyType& key)
    {
        std::unique_lock lock(mutex_);
        return registry_.erase(key) > 0;
    }

    // Delete copy and move operations for safety
    Registry(const Registry&)            = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&)                 = delete;
    Registry& operator=(Registry&&)      = delete;

private:
    std::unordered_map<KeyType, Function> registry_;
    mutable std::shared_mutex             mutex_;  // shared_mutex for read-write locking
};

//-----------------------------------------------------------------------------
// Registerer helper class
//-----------------------------------------------------------------------------

template <typename KeyType, typename Function>
class Registerer
{
public:
    explicit Registerer(const KeyType& key, Registry<KeyType, Function>* registry, Function method)
    {
        if (registry)
        {
            registry->Register(key, std::move(method));
        }
    }

    // Move-based constructor
    explicit Registerer(KeyType&& key, Registry<KeyType, Function>* registry, Function method)
    {
        if (registry)
        {
            registry->Register(std::move(key), std::move(method));
        }
    }
};

//-----------------------------------------------------------------------------
// Creator Registry for object factories
//-----------------------------------------------------------------------------

namespace creator
{

template <typename KeyType, typename ReturnType, typename... Args>
class Registry
{
public:
    using Function = std::function<ReturnType(Args...)>;

    Registry() = default;

    void Register(const KeyType& key, Function f)
    {
        std::unique_lock lock(mutex_);
        registry_[key] = std::move(f);
    }

    void Register(KeyType&& key, Function f)
    {
        std::unique_lock lock(mutex_);
        registry_[std::move(key)] = std::move(f);
    }

    [[nodiscard]] bool Has(const KeyType& key) const
    {
        std::shared_lock lock(mutex_);
        return registry_.contains(key);
    }

    /**
     * @brief Create an object using registered factory
     * @return Created object or nullptr if key not found
     */
    ReturnType Create(const KeyType& key, Args... args) const
    {
        std::shared_lock lock(mutex_);
        auto             it = registry_.find(key);
        if (it == registry_.end())
        {
            return nullptr;
        }
        return it->second(args...);
    }

    // Legacy interface
    ReturnType run(const KeyType& key, Args... args) const { return Create(key, args...); }

    [[nodiscard]] std::vector<KeyType> Keys() const
    {
        std::shared_lock     lock(mutex_);
        std::vector<KeyType> keys;
        keys.reserve(registry_.size());

        for (const auto& [key, _] : registry_)
        {
            keys.push_back(key);
        }
        return keys;
    }

    [[nodiscard]] std::size_t Size() const
    {
        std::shared_lock lock(mutex_);
        return registry_.size();
    }

    void Clear()
    {
        std::unique_lock lock(mutex_);
        registry_.clear();
    }

    Registry(const Registry&)            = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&)                 = delete;
    Registry& operator=(Registry&&)      = delete;

private:
    std::unordered_map<KeyType, Function> registry_;
    mutable std::shared_mutex             mutex_;
};

template <typename KeyType, typename ReturnType, typename... Args>
class Registerer
{
public:
    explicit Registerer(
        const KeyType&                                            key,
        Registry<KeyType, ReturnType, Args...>*                   registry,
        typename Registry<KeyType, ReturnType, Args...>::Function method)
    {
        if (registry)
        {
            registry->Register(key, std::move(method));
        }
    }

    template <typename DerivedType>
    static ReturnType DefaultCreator(Args... args)
    {
        return ReturnType(new DerivedType(args...));
    }
};

}  // namespace creator

//-----------------------------------------------------------------------------
// Singleton Registry Pattern (optional, for global registries)
//-----------------------------------------------------------------------------

template <typename KeyType, typename Function>
class SingletonRegistry
{
public:
    static Registry<KeyType, Function>& Instance()
    {
        static Registry<KeyType, Function> instance;
        return instance;
    }

    SingletonRegistry()                                    = delete;
    SingletonRegistry(const SingletonRegistry&)            = delete;
    SingletonRegistry& operator=(const SingletonRegistry&) = delete;
};

//-----------------------------------------------------------------------------
// Macro definitions (compatible with original API)
//-----------------------------------------------------------------------------

#define SERIALIZATION_DECLARE_FUNCTION_REGISTRY(RegistryName, Function) \
    serialization::Registry<std::string, Function>* RegistryName();     \
    using Registerer##RegistryName = serialization::Registerer<std::string, Function>;

#define SERIALIZATION_DEFINE_FUNCTION_REGISTRY(RegistryName, Function)                \
    serialization::Registry<std::string, Function>* RegistryName()                    \
    {                                                                                 \
        static auto* registry = new serialization::Registry<std::string, Function>(); \
        return registry;                                                              \
    }

#define SERIALIZATION_REGISTER_FUNCTION(RegistryName, type, Function)                   \
    static Registerer##RegistryName SERIALIZATION_ANONYMOUS_VARIABLE(g_##RegistryName)( \
        serialization::demangle(typeid(type).name()), RegistryName(), Function);

#define SERIALIZATION_DECLARE_TYPED_REGISTRY(RegistryName, KeyType, ObjectType, PtrType, ...)      \
    serialization::creator::Registry<KeyType, PtrType<ObjectType>, ##__VA_ARGS__>* RegistryName(); \
    using Registerer##RegistryName =                                                               \
        serialization::creator::Registerer<KeyType, PtrType<ObjectType>, ##__VA_ARGS__>;

#define SERIALIZATION_DEFINE_TYPED_REGISTRY(RegistryName, KeyType, ObjectType, PtrType, ...)      \
    serialization::creator::Registry<KeyType, PtrType<ObjectType>, ##__VA_ARGS__>* RegistryName() \
    {                                                                                             \
        static auto* registry =                                                                   \
            new serialization::creator::Registry<KeyType, PtrType<ObjectType>, ##__VA_ARGS__>();  \
        return registry;                                                                          \
    }

#define SERIALIZATION_REGISTER_TYPED_CREATOR(RegistryName, key, ...)                    \
    static Registerer##RegistryName SERIALIZATION_ANONYMOUS_VARIABLE(g_##RegistryName)( \
        key, RegistryName(), ##__VA_ARGS__);

#define SERIALIZATION_REGISTER_TYPED_CLASS(RegistryName, key, ...)                      \
    static Registerer##RegistryName SERIALIZATION_ANONYMOUS_VARIABLE(g_##RegistryName)( \
        key, RegistryName(), Registerer##RegistryName::DefaultCreator<__VA_ARGS__>);

#define SERIALIZATION_DECLARE_REGISTRY(RegistryName, ObjectType, ...) \
    SERIALIZATION_DECLARE_TYPED_REGISTRY(                             \
        RegistryName, std::string, ObjectType, std::unique_ptr, ##__VA_ARGS__)

#define SERIALIZATION_DEFINE_REGISTRY(RegistryName, ObjectType, ...) \
    SERIALIZATION_DEFINE_TYPED_REGISTRY(                             \
        RegistryName, std::string, ObjectType, std::unique_ptr, ##__VA_ARGS__)

#define SERIALIZATION_REGISTER_CREATOR(RegistryName, key, ...) \
    SERIALIZATION_REGISTER_TYPED_CREATOR(RegistryName, #key, __VA_ARGS__)

#define SERIALIZATION_REGISTER_CLASS(RegistryName, key, ...) \
    SERIALIZATION_REGISTER_TYPED_CLASS(RegistryName, #key, __VA_ARGS__)

#define SERIALIZATION_DECLARE_SHARED_REGISTRY(RegistryName, ObjectType, ...) \
    SERIALIZATION_DECLARE_TYPED_REGISTRY(                                    \
        RegistryName, std::string, ObjectType, std::shared_ptr, ##__VA_ARGS__)

#define SERIALIZATION_DEFINE_SHARED_REGISTRY(RegistryName, ObjectType, ...) \
    SERIALIZATION_DEFINE_TYPED_REGISTRY(                                    \
        RegistryName, std::string, ObjectType, std::shared_ptr, ##__VA_ARGS__)

}  // namespace serialization
