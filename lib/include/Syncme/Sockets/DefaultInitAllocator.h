#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace Syncme
{
  namespace Sockets
  {
    // std::allocator whose no-argument construct() default-initializes
    // instead of value-initializing. For a trivial type like char, that
    // means growing a vector<char, DefaultInitAllocator<char>> via the
    // sizing constructor or resize() leaves the new elements indeterminate
    // instead of zero-filling them (no memset).
    //
    // Only safe for a buffer whose newly-grown range is always written
    // (fully, or captured by an explicit byte count that is checked before
    // the range is read) before anyone reads it -- never for a buffer whose
    // unwritten tail is meant to read as zero.
    template <class T>
    struct DefaultInitAllocator : std::allocator<T>
    {
      using Base = std::allocator<T>;

      template <class U>
      struct rebind
      {
        using other = DefaultInitAllocator<U>;
      };

      DefaultInitAllocator() noexcept = default;

      template <class U>
      DefaultInitAllocator(const DefaultInitAllocator<U>&) noexcept
      {
      }

      template <class U>
      void construct(U* p) noexcept(std::is_nothrow_default_constructible<U>::value)
      {
        ::new (static_cast<void*>(p)) U;
      }

      template <class U, class... Args>
      void construct(U* p, Args&&... args)
      {
        std::allocator_traits<Base>::construct(
          static_cast<Base&>(*this)
          , p
          , std::forward<Args>(args)...
        );
      }
    };
  }
}
