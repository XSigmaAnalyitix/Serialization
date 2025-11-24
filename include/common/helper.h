#pragma once

#ifndef __SERIALIZATION_WRAP__

#include <new>      // for operator new
#include <utility>  // for forward

#include "common/pointer.h"  // for shared_ptr, unique_ptr

namespace serialization
{
namespace access
{
struct serilizer
{
    /**
     * Allows placement construction of types.
     */
    template <typename Item, typename... Arguments>
    static auto placement_new(void* pAddress, Arguments&&... arguments) noexcept(
        noexcept(Item(std::forward<Arguments>(arguments)...)))
    {
        return ::new (pAddress) Item(std::forward<Arguments>(arguments)...);
    }

    /**
     * Allows destruction of types.
     */
    template <typename Item>
    static void destruct(Item& item) noexcept
    {
        item.~Item();
    }

    template <typename T>
    inline static auto make_ptr()
    {
        return std::unique_ptr<T>(new T());
    }

    template <typename T>
    inline static auto make_shard_ptr()
    {
        return std::shared_ptr<T>(new T());
    }

    template <typename T>
    inline static void initialize(T& obj)
    {
        obj.initialize();
    }

    template <typename T>
    constexpr static auto tuple() -> decltype(T::properties())
    {
        return T::properties();
    }
};
}  // namespace access
}  // namespace serialization
#endif
