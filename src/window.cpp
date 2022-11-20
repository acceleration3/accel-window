#include <accel/window.hpp>

#if defined(PLATFORM_WINDOWS)
	#include <accel/impls/win32_window.inl>
#elif defined(PLATFORM_LINUX)

#else
	#error "No window implementation for the current platform."
#endif
