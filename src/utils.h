#pragma once

namespace Typhoon {

namespace Jobs {

namespace detail {

inline uintptr_t alignPointer(uintptr_t value, size_t alignment) {
	return (value + (alignment - 1)) & (~(alignment - 1));
}

inline void* alignPointer(void* ptr, size_t alignment) {
	return reinterpret_cast<void*>(alignPointer(reinterpret_cast<uintptr_t>(ptr), alignment));
}

inline constexpr bool isPowerOfTwo(uint32_t v) {
	return 0 == (v & (v - 1));
}

inline constexpr uint32_t nextPowerOfTwo(uint32_t v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

} // namespace detail

} // namespace Jobs

} // namespace Typhoon
