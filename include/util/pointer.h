#pragma once

#include <memory>

namespace serialization
{
template <typename T>
using ptr_const = std::shared_ptr<const T>;

template <typename T>
using ptr_mutable = std::shared_ptr<T>;

template <typename T>
using ptr_unique_const = std::unique_ptr<const T>;

template <typename T>
using ptr_unique_mutable = std::unique_ptr<T>;

namespace util
{
template <typename T, typename... Args>
std::shared_ptr<const T> make_ptr_const(Args&&... args)
{
    return std::make_shared<const T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::shared_ptr<T> make_ptr_mutable(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::unique_ptr<const T> make_ptr_unique_const(Args&&... args)
{
    return std::make_unique<const T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::unique_ptr<T> make_ptr_unique_mutable(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::shared_ptr<T> make_shared(Args&&... args)
{
    return std::make_shared<T>(new T(std::forward<Args>(args)...));
}

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
}  // namespace util
}  // namespace serialization
