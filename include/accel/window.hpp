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

		window(const window_create_params& params);
		~window();

		size2u client_size() const;
		size2u size() const;
		rectanglei rect() const;
		point2i position() const;
		bool closing() const;
		bool resizable() const;
		bool undecorated() const;
		bool hidden() const;
		bool hide_mouse() const;
		bool trap_mouse() const;
		flagset<window_style_bits> style() const;
		std::string title() const;

		void set_title(std::string_view title);
		void set_position(const point2i& position);
		void set_size(const size2u& size);
		void set_client_size(const size2u& size);
		void set_rect(const rectanglei& rect);
		void set_resizable(bool state);
		void set_undecorated(bool state);
		void set_hidden(bool state);
		void set_hide_mouse(bool state);
		void set_trap_mouse(bool state);
		void set_style(const flagset<window_style_bits>& style);

		void pump_events();
		std::vector<any_event> poll_events();

		template<typename T> T* platform_handle() const { return reinterpret_cast<T*>(handle()); }

	private:
		std::unique_ptr<impl> m_impl;
		void* handle() const;
	};
}

#endif