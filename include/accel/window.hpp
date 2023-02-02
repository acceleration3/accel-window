#ifndef ACCEL_WINDOW_H
#define ACCEL_WINDOW_H

#include <cstdint>

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <variant>

#include <accel/macros.hpp>
#include <accel/flagset.hpp>
#include <accel/math.hpp>

namespace accel
{
	enum class mouse_buttons
	{
		left,
		middle,
		right,
		backwards,
		forwards
	};

	struct mouse_click_event
	{
		bool is_mouse_down;
		mouse_buttons button;
		point2i position;
	};

	struct mouse_move_event
	{
		point2i position;
	};

	struct mouse_scroll_event
	{
		point2i position;
		int scroll_lines;
	};

	struct key_event
	{
		bool is_key_down;
		int keycode;
		int repeat;
	};

	struct resize_event
	{
		size2u size;
		size2u client_size;
	};

	using any_event = std::variant<
		mouse_click_event,
		mouse_move_event,
		mouse_scroll_event,
		key_event,
		resize_event
	>;

	enum class window_style_bits
	{
		resizable,
		undecorated,
		hidden,
		hide_mouse,
		trap_mouse,
		_
	};

	struct window_create_params
	{
		std::string_view title;
		size2u size;
		flagset<window_style_bits> style;
	};

	class window
	{
	public:
		struct impl;

		ACC_EXPORT window(const window_create_params& params);
		ACC_EXPORT ~window();

		window(const window&) = delete;
		window& operator=(const window&) = delete;

		ACC_EXPORT window(window&&);
		ACC_EXPORT window& operator=(window&&);

		ACC_EXPORT size2u client_size() const;
		ACC_EXPORT size2u size() const;
		ACC_EXPORT rectanglei rect() const;
		ACC_EXPORT point2i position() const;
		ACC_EXPORT bool closing() const;
		ACC_EXPORT bool resizable() const;
		ACC_EXPORT bool undecorated() const;
		ACC_EXPORT bool hidden() const;
		ACC_EXPORT bool hide_mouse() const;
		ACC_EXPORT bool trap_mouse() const;
		ACC_EXPORT flagset<window_style_bits> style() const;
		ACC_EXPORT std::string title() const;

		ACC_EXPORT void set_title(std::string_view title);
		ACC_EXPORT void set_position(const point2i& position);
		ACC_EXPORT void set_size(const size2u& size);
		ACC_EXPORT void set_client_size(const size2u& size);
		ACC_EXPORT void set_rect(const rectanglei& rect);
		ACC_EXPORT void set_resizable(bool state);
		ACC_EXPORT void set_undecorated(bool state);
		ACC_EXPORT void set_hidden(bool state);
		ACC_EXPORT void set_hide_mouse(bool state);
		ACC_EXPORT void set_trap_mouse(bool state);
		ACC_EXPORT void set_style(const flagset<window_style_bits>& style);

		ACC_EXPORT void pump_events();
		ACC_EXPORT std::vector<any_event> poll_events();

		template<typename T> T platform_handle() const { return reinterpret_cast<T>(handle()); }

	private:
		std::unique_ptr<impl> m_impl;		
		ACC_EXPORT void* handle() const;
	};
}

#endif