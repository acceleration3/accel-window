#ifndef ACCEL_WINDOW_H
#define ACCEL_WINDOW_H

#include <cstdint>

#include <string>
#include <string_view>
#include <memory>

#include <accel/macros.hpp>
#include <accel/flagset.hpp>
#include <accel/math.hpp>

namespace accel
{
	enum class window_style_flags
	{
		resizable,
		decorated,
		hidden,
		hide_mouse,
		trap_mouse,
		_
	};

	struct window_create_params
	{
		std::string_view title;
		flagset<window_style_flags> flags;
	};

	class window
	{
	public:


	private:

	};
}

#endif