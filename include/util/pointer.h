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
