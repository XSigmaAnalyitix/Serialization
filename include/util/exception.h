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

#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace xsigma
{

// Helper to fold variadic arguments into stream
namespace detail
{
template <typename Stream, typename... Args>
void stream_args(Stream& stream, Args&&... args)
{
    (stream << ... << std::forward<Args>(args));
}
}  // namespace detail

// Debug check macro - in debug mode throws exception, in release mode does nothing
#ifdef NDEBUG
#define SERIALIZATION_CHECK_DEBUG(condition, ...) ((void)0)
#else
#define SERIALIZATION_CHECK_DEBUG(condition, ...)                                          \
    do                                                                              \
    {                                                                               \
        if (!(condition))                                                           \
        {                                                                           \
            std::ostringstream oss;                                                 \
            oss << "Check failed: " << #condition << " ";                           \
            ::xsigma::detail::stream_args(oss, __VA_ARGS__);                        \
            throw std::runtime_error(oss.str());                                    \
        }                                                                           \
    } while (0)
#endif

}  // namespace xsigma
