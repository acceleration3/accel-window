#include <unordered_map>
#include <algorithm>

#define UNICODE
#include <Windows.h>
#include <windowsx.h>

#include <accel/details/LooplessSizeMove.h>

#define WINDOW_CLASS TEXT("AccelWindow")

namespace accel
{
	struct window::impl
	{
		HWND hwnd;
        bool closing;
        flagset<window_style_bits> style;
        std::vector<any_event> events;
        point2i last_position;
        int scroll_amount;

        impl() : 
            hwnd(nullptr), 
            closing(false),
            style(),
            last_position(),
            scroll_amount(0) {}
	};

    static HKL g_layout = nullptr;

    static RECT get_real_rect(HWND hwnd)
    {
        RECT rc;
        using api_call = HRESULT(*)(HWND, DWORD, PVOID, DWORD);

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        static api_call get_rect = reinterpret_cast<api_call>(GetProcAddress(LoadLibrary(TEXT("dwmapi.dll")), "DwmGetWindowAttribute"));
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif

        if (get_rect)
        {
            // Windows Vista+
            HRESULT res = get_rect(hwnd, 9, &rc, sizeof(rc));
            if (res == S_OK)
                return rc;
            else
                throw std::runtime_error("DwmGetWindowAttribute failed.");
        }

        // Fallback
        GetWindowRect(hwnd, &rc);
        return rc;
    }

    static void trap_mouse(HWND hwnd)
    {
        ReleaseCapture();
        SetCapture(hwnd);

        RECT rect;
        GetClientRect(hwnd, &rect);
        MapWindowPoints(hwnd, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&rect), 2);
        ClipCursor(&rect);
    }

    static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        static std::unordered_map<HWND, window::impl*> window_map;
        window::impl* impl = nullptr;

        if (msg == WM_CREATE)
        {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
            impl = reinterpret_cast<window::impl*>(cs->lpCreateParams);
            window_map[hwnd] = impl;
        }
        else
        {
            impl = window_map[hwnd];
        }

        if (msg == WM_SYSCOMMAND)
        {
            int command = wparam & 0xFFF0;
            if (command == SC_MOUSEMENU || command == SC_KEYMENU)
                return TRUE;
        }

        switch (msg)
        {
            case WM_SYSKEYUP:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_KEYDOWN:
            {
                unsigned int scancode = (lparam >> 16) & 0xFF;
                unsigned int extended = (lparam >> 24) & 0x1;
                bool is_key_down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);

                WPARAM keycode = wparam;
                switch (keycode)
                {
                    case VK_CONTROL: keycode = extended ? VK_RCONTROL : VK_LCONTROL; break;
                    case VK_MENU: keycode = extended ? VK_RMENU : VK_LMENU; break;
                    case VK_INSERT:
                    case VK_END:
                    case VK_NEXT:
                    case VK_HOME:
                    case VK_PRIOR:
                    case VK_DELETE:
                    case VK_SHIFT:
                        keycode = MapVirtualKeyEx(scancode, MAPVK_VSC_TO_VK_EX, g_layout);
                        break;
                }

                impl->events.emplace_back(key_event{ is_key_down, static_cast<int>(keycode), static_cast<int>(lparam & 0xFFFF) });
                break;
            }

            case WM_LBUTTONUP:
            case WM_MBUTTONUP:
            case WM_RBUTTONUP:
            case WM_XBUTTONUP:
            case WM_LBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_XBUTTONDOWN:
            {
                int x_pos = GET_X_LPARAM(lparam);
                int y_pos = GET_Y_LPARAM(lparam);
                
                mouse_buttons button;
                if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)
                    button = mouse_buttons::left;
                else if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP)
                    button = mouse_buttons::middle;
                else if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP)
                    button = mouse_buttons::right;
                else
                    button = GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? mouse_buttons::backwards : mouse_buttons::forwards;

                bool is_mouse_down;
                if (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_XBUTTONDOWN)
                    is_mouse_down = true;
                else
                    is_mouse_down = false;

                impl->events.emplace_back(mouse_click_event{ is_mouse_down, button, point2i(x_pos, y_pos) });
                break;
            }

            case WM_CLOSE:
                impl->closing = true;
                break;

            case WM_DESTROY:
                PostQuitMessage(0);
                break;

            case WM_ACTIVATE:
                if (wparam == WA_INACTIVE)
                {
                    if (impl->style & window_style_bits::trap_mouse)
                        ClipCursor(nullptr);
                } 
                else
                {
                    if (impl->style & window_style_bits::trap_mouse)
                        accel::trap_mouse(impl->hwnd);
                }
                break;

            case WM_CONTEXTMENU:
                return TRUE;

            case WM_MOUSEMOVE:
            {
                point2i mouse_position(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                impl->events.emplace_back(mouse_move_event{ mouse_position });
                break;
            }
                
            case WM_MOVE: break;

            case WM_MOUSEWHEEL:
            {
                UINT lines_per_tick = 0;
                SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_tick, 0);

                impl->scroll_amount += GET_WHEEL_DELTA_WPARAM(wparam);

                bool is_negative = impl->scroll_amount < 0;
                int scroll_ticks = 0;
                while (std::abs(impl->scroll_amount) >= WHEEL_DELTA)
                {
                    scroll_ticks += is_negative ? -1 : 1;
                    impl->scroll_amount += is_negative ? WHEEL_DELTA : -WHEEL_DELTA;
                }

                if (scroll_ticks != 0)
                {
                    point2i mouse_position(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                    impl->events.emplace_back(mouse_scroll_event{ mouse_position, scroll_ticks * static_cast<int>(lines_per_tick) });
                }
                break;
            }

            case WM_SIZE:
            {
                RECT rect;
                GetWindowRect(hwnd, &rect);

                RECT client_rect;
                GetClientRect(hwnd, &client_rect);

                size2u size(static_cast<unsigned>(rect.right - rect.left), static_cast<unsigned>(rect.bottom - rect.top));
                size2u client_size(static_cast<unsigned>(client_rect.right - client_rect.left), static_cast<unsigned>(client_rect.bottom - client_rect.top));

                impl->events.emplace_back(resize_event{ size, client_size });
            }
        }

        //return DefWindowProc(hwnd, msg, wparam, lparam);
        return LSMProc(hwnd, msg, wparam, lparam);
    }

    static std::string utf8_encode(std::wstring_view wstr)
    {
        if (wstr.empty()) 
            return std::string();

        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
        std::string conv(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), conv.data(), size, NULL, NULL);
        
        return conv;
    }

    static std::wstring utf8_decode(std::string_view str)
    {
        if (str.empty()) 
            return std::wstring();

        int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
        std::wstring conv(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), conv.data(), size);
        
        return conv;
    }

	window::window(const window_create_params& params) :
        m_impl(std::make_unique<impl>())
	{
        static HINSTANCE hinstance = GetModuleHandle(nullptr);
		static bool initialized = false;
		if (!initialized)
		{
			WNDCLASS cls{};
			cls.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
			cls.lpszClassName = WINDOW_CLASS;
			cls.lpfnWndProc = &wndproc;
			cls.hInstance = GetModuleHandle(NULL);
			cls.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
			cls.hCursor = LoadCursor(NULL, IDC_ARROW);
            if (!RegisterClass(&cls))
                throw std::runtime_error("Failed to register window class.");

            TCHAR name[KL_NAMELENGTH];
            GetKeyboardLayoutName(name);
            g_layout = LoadKeyboardLayout(TEXT("00000409"), KLF_NOTELLSHELL);
            LoadKeyboardLayout(name, KLF_ACTIVATE);
            initialized = true;
		}
     
        std::wstring title = utf8_decode(params.title);
        m_impl->hwnd = CreateWindow(WINDOW_CLASS, title.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, hinstance, reinterpret_cast<LPVOID>(m_impl.get()));
        if (!m_impl->hwnd)
            throw std::runtime_error("Failed to create window.");

        ShowWindow(m_impl->hwnd, SW_NORMAL);
        set_style(params.style);
        set_size(params.size);
	}

	window::~window()
	{
        if (m_impl && m_impl->hwnd)
            DestroyWindow(m_impl->hwnd);
	}

	size2u window::client_size() const
	{
		RECT rect;
		GetClientRect(m_impl->hwnd, &rect);
		return size2u(static_cast<unsigned>(rect.right - rect.left), static_cast<unsigned>(rect.bottom - rect.top));
	}

	size2u window::size() const
	{
		RECT rect = get_real_rect(m_impl->hwnd);
        auto width = (std::max)(rect.right - rect.left, LONG(0));
        auto height = (std::max)(rect.bottom - rect.top, LONG(0));
        return size2u(static_cast<unsigned>(width), static_cast<unsigned>(height));
	}

	rectanglei window::rect() const
	{
        RECT rect = get_real_rect(m_impl->hwnd);
        return rectanglei(rect.top, rect.left, rect.bottom, rect.right);
	}

	point2i window::position() const
	{
        RECT rect = get_real_rect(m_impl->hwnd);
        return point2i(rect.left, rect.top);
	}

	bool window::closing() const
	{
        return m_impl->closing;
	}

    bool window::resizable() const
    {
        return m_impl->style[window_style_bits::resizable];
    }

    bool window::undecorated() const
    {
        return m_impl->style[window_style_bits::undecorated];
    }

    bool window::hidden() const
    {
        return m_impl->style[window_style_bits::hidden];
    }

    bool window::hide_mouse() const
    {
        return m_impl->style[window_style_bits::hide_mouse];
    }

    bool window::trap_mouse() const
    {
        return m_impl->style[window_style_bits::trap_mouse];
    }

	flagset<window_style_bits> window::style() const
	{
        return flagset<window_style_bits>();
	}

	std::string window::title() const
	{
        int size = GetWindowTextLengthW(m_impl->hwnd) + 1;
        std::wstring title(size, '\0');
        GetWindowTextW(m_impl->hwnd, title.data(), static_cast<int>(title.size()));
        return utf8_encode(title);
	}

    void window::set_title(std::string_view title)
    {
        auto wide_title = utf8_decode(title);
        SetWindowTextW(m_impl->hwnd, wide_title.c_str());
    }

    void window::set_position(const point2i& position)
	{
		RECT excluding_shadow = get_real_rect(m_impl->hwnd);

		RECT including_shadow = {};
		GetWindowRect(m_impl->hwnd, &including_shadow);

		int shadow_left = including_shadow.left - excluding_shadow.left;
		int shadow_top = including_shadow.top - excluding_shadow.top;
		SetWindowPos(m_impl->hwnd, 0, position.x() + shadow_left, position.y() + shadow_top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_impl->style & window_style_bits::trap_mouse)
            accel::trap_mouse(m_impl->hwnd);
	}

    void window::set_size(const size2u& size)
    {
        RECT excluding_shadow = get_real_rect(m_impl->hwnd);
        
        RECT including_shadow;
        GetWindowRect(m_impl->hwnd, &including_shadow);
        
        int shadow_width = (including_shadow.right - excluding_shadow.right) + (excluding_shadow.left - including_shadow.left);
        int shadow_height = (including_shadow.bottom - excluding_shadow.bottom) + (excluding_shadow.top - including_shadow.top);
        SetWindowPos(m_impl->hwnd, 0, 0, 0, size.width() + shadow_width, size.height() + shadow_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_impl->style & window_style_bits::trap_mouse)
            accel::trap_mouse(m_impl->hwnd);
    }

    void window::set_client_size(const size2u& size)
    {
        RECT rect{};
        rect.right = size.width();
        rect.bottom = size.height();

        DWORD style = GetWindowLong(m_impl->hwnd, GWL_STYLE);
        AdjustWindowRect(&rect, style, FALSE);
        
        SetWindowPos(m_impl->hwnd, 0, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_impl->style & window_style_bits::trap_mouse)
            accel::trap_mouse(m_impl->hwnd);
    }

    void window::set_rect(const rectanglei& rect)
    {
        set_position(rect.top_left());
        set_size(size2u(static_cast<unsigned>(rect.size().width()), static_cast<unsigned>(rect.size().height())));
    }

    void window::set_resizable(bool state)
    {
        LONG_PTR style = GetWindowLongPtr(m_impl->hwnd, GWL_STYLE);

        if (state)
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
        else
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

        SetWindowLongPtr(m_impl->hwnd, GWL_STYLE, style);
        m_impl->style.set(window_style_bits::resizable, state);
    }

    void window::set_undecorated(bool state)
    {
        LONG_PTR style = GetWindowLongPtr(m_impl->hwnd, GWL_STYLE);

        if (state)
            style &= ~(WS_CAPTION | WS_MINIMIZEBOX);
        else
            style |= WS_CAPTION | WS_MINIMIZEBOX;

        SetWindowLongPtr(m_impl->hwnd, GWL_STYLE, style);
        m_impl->style.set(window_style_bits::undecorated, state);
    }

    void window::set_hidden(bool state)
    {
        if (state)
            ShowWindow(m_impl->hwnd, SW_HIDE);
        else
            ShowWindow(m_impl->hwnd, SW_SHOWNA);

        m_impl->style.set(window_style_bits::hidden, state);
    }

    void window::set_hide_mouse(bool state)
    {
        if (state)
            ShowCursor(FALSE);
        else
            ShowCursor(TRUE);

        m_impl->style.set(window_style_bits::hide_mouse, state);
    }

    void window::set_trap_mouse(bool state)
    {
        if (state)
            accel::trap_mouse(m_impl->hwnd);
        else
            ClipCursor(nullptr);

        m_impl->style.set(window_style_bits::trap_mouse, state);
    }

    void window::set_style(const flagset<window_style_bits>& style)
    {
        set_resizable(style[window_style_bits::resizable]);
        set_undecorated(style[window_style_bits::undecorated]);
        set_hidden(style[window_style_bits::hidden]);
        set_hide_mouse(style[window_style_bits::hide_mouse]);
        set_trap_mouse(style[window_style_bits::trap_mouse]);
    }

    void window::pump_events()
    {
        MSG msg;
        while (PeekMessage(&msg, m_impl->hwnd, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            SizingCheck(&msg);
            DispatchMessage(&msg);
        }
    }

    std::vector<any_event> window::poll_events()
    {
        return std::move(m_impl->events);
    }

    void* window::handle() const
    {
        return reinterpret_cast<void*>(m_impl->hwnd);
    }
}