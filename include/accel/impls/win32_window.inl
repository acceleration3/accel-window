#include <unordered_map>
#include <algorithm>

#define UNICODE
#include <Windows.h>
#include <windowsx.h>

#define WINDOW_CLASS TEXT("AccelWindow")

namespace accel
{
    using native_handle_t = HWND;

    class window;

    namespace details
    {
        static RECT get_real_rect(HWND hwnd)
        {
            RECT rc{};
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

        static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        static std::string utf8_encode(const std::wstring& wstr)
        {
            if (wstr.empty())
                return std::string();

            int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
            std::string conv(size, 0);
            WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), conv.data(), size, NULL, NULL);

            return conv;
        }

        static std::wstring utf8_decode(const std::string& str)
        {
            if (str.empty())
                return std::wstring();

            int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
            std::wstring conv(size, 0);
            MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), conv.data(), size);

            return conv;
        }
    }

    class window
    {
    public:
        window(const window_create_params& params) : 
            m_closing(false),
            m_hwnd(nullptr),
            m_scroll_amount(0)
        {
            static HINSTANCE hinstance = GetModuleHandle(nullptr);
            static bool initialized = false;
            if (!initialized)
            {
                WNDCLASS cls{};
                cls.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
                cls.lpszClassName = WINDOW_CLASS;
                cls.lpfnWndProc = &details::wndproc;
                cls.hInstance = GetModuleHandle(NULL);
                cls.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
                cls.hCursor = LoadCursor(NULL, IDC_ARROW);
                if (!RegisterClass(&cls))
                    throw std::runtime_error("Failed to register window class.");

                initialized = true;
            }

            TCHAR name[KL_NAMELENGTH];
            GetKeyboardLayoutName(name);
            m_layout = LoadKeyboardLayout(TEXT("00000409"), KLF_NOTELLSHELL);
            LoadKeyboardLayout(name, KLF_ACTIVATE);

            std::wstring title = details::utf8_decode(params.title);
            m_hwnd = CreateWindow(WINDOW_CLASS, title.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, params.size.width(), params.size.height(), nullptr, nullptr, hinstance, reinterpret_cast<LPVOID>(this));
            if (!m_hwnd)
                throw std::runtime_error("Failed to create window.");

            ShowWindow(m_hwnd, SW_NORMAL);
            set_style(params.style);
            set_size(params.size);
        }

        ~window()
        {
            if (m_hwnd)
                DestroyWindow(m_hwnd);
        }

        window(const window&) = delete;
        window& operator=(const window&) = delete;

        window(window&&) = default;
        window& operator=(window&&) = default;

        size2u get_client_size() const
        {
            RECT rect;
            GetClientRect(m_hwnd, &rect);
            return size2u(static_cast<unsigned>(rect.right - rect.left), static_cast<unsigned>(rect.bottom - rect.top));
        }

        size2u get_size() const
        {
            RECT rect = details::get_real_rect(m_hwnd);
            auto width = (std::max)(rect.right - rect.left, LONG(0));
            auto height = (std::max)(rect.bottom - rect.top, LONG(0));
            return size2u(static_cast<unsigned>(width), static_cast<unsigned>(height));
        }

        rectanglei get_rect() const
        {
            RECT rect = details::get_real_rect(m_hwnd);
            return rectanglei(rect.top, rect.left, rect.bottom, rect.right);
        }

        point2i get_position() const
        {
            RECT rect = details::get_real_rect(m_hwnd);
            return point2i(rect.left, rect.top);
        }

        bool is_closing() const { return m_closing; }
        bool is_resizable() const { return m_style[window_style_bits::resizable]; }
        bool is_undecorated() const { return m_style[window_style_bits::undecorated]; }
        bool is_hidden() const { return m_style[window_style_bits::undecorated]; }
        bool is_hiding_mouse() const { return m_style[window_style_bits::hide_mouse]; }
        bool is_trapping_mouse() const { return m_style[window_style_bits::trap_mouse]; }
        flagset<window_style_bits> get_style() const { return m_style; }
        
        std::string get_title() const
        {
            int size = GetWindowTextLengthW(m_hwnd) + 1;
            std::wstring title(size, '\0');
            GetWindowTextW(m_hwnd, title.data(), static_cast<int>(title.size()));
            return details::utf8_encode(title);
        }

        void set_title(const std::string& title)
        {
            auto wide_title = details::utf8_decode(title);
            SetWindowTextW(m_hwnd, wide_title.c_str());
        }

        void set_position(const point2i& position)
        {
            RECT excluding_shadow = details::get_real_rect(m_hwnd);

            RECT including_shadow = {};
            GetWindowRect(m_hwnd, &including_shadow);

            int shadow_left = including_shadow.left - excluding_shadow.left;
            int shadow_top = including_shadow.top - excluding_shadow.top;
            SetWindowPos(m_hwnd, 0, position.x() + shadow_left, position.y() + shadow_top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

            if (m_style & window_style_bits::trap_mouse)
                details::trap_mouse(m_hwnd);
        }

        void set_size(const size2u& size)
        {
            RECT excluding_shadow = details::get_real_rect(m_hwnd);

            RECT including_shadow;
            GetWindowRect(m_hwnd, &including_shadow);

            int shadow_width = (including_shadow.right - excluding_shadow.right) + (excluding_shadow.left - including_shadow.left);
            int shadow_height = (including_shadow.bottom - excluding_shadow.bottom) + (excluding_shadow.top - including_shadow.top);
            SetWindowPos(m_hwnd, 0, 0, 0, size.width() + shadow_width, size.height() + shadow_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

            if (m_style & window_style_bits::trap_mouse)
                details::trap_mouse(m_hwnd);
        }


        void set_client_size(const size2u& size)
        {
            RECT rect{};
            rect.right = size.width();
            rect.bottom = size.height();

            DWORD style = GetWindowLong(m_hwnd, GWL_STYLE);
            AdjustWindowRect(&rect, style, FALSE);

            SetWindowPos(m_hwnd, 0, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

            if (m_style & window_style_bits::trap_mouse)
                details::trap_mouse(m_hwnd);
        }

        void set_rect(const rectanglei& rect)
        {
            set_position(rect.top_left());
            set_size(size2u(static_cast<unsigned>(rect.size().width()), static_cast<unsigned>(rect.size().height())));
        }

        void set_resizable(bool state)
        {
            LONG_PTR style = GetWindowLongPtr(m_hwnd, GWL_STYLE);

            if (state)
                style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            else
                style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

            SetWindowLongPtr(m_hwnd, GWL_STYLE, style);
            m_style.set(window_style_bits::resizable, state);
        }

        void set_undecorated(bool state)
        {
            LONG_PTR style = GetWindowLongPtr(m_hwnd, GWL_STYLE);

            if (state)
                style &= ~(WS_CAPTION | WS_MINIMIZEBOX);
            else
                style |= WS_CAPTION | WS_MINIMIZEBOX;

            SetWindowLongPtr(m_hwnd, GWL_STYLE, style);
            m_style.set(window_style_bits::undecorated, state);
        }

        void set_hidden(bool state)
        {
            if (state)
                ShowWindow(m_hwnd, SW_HIDE);
            else
                ShowWindow(m_hwnd, SW_SHOWNA);

            m_style.set(window_style_bits::hidden, state);
        }

        void set_hide_mouse(bool state)
        {
            if (state)
                ShowCursor(FALSE);
            else
                ShowCursor(TRUE);

            m_style.set(window_style_bits::hide_mouse, state);
        }

        void set_trap_mouse(bool state)
        {
            if (state)
                details::trap_mouse(m_hwnd);
            else
                ClipCursor(nullptr);

            m_style.set(window_style_bits::trap_mouse, state);
        }

        void set_style(const flagset<window_style_bits>& style)
        {
            set_resizable(style[window_style_bits::resizable]);
            set_undecorated(style[window_style_bits::undecorated]);
            set_hidden(style[window_style_bits::hidden]);
            set_hide_mouse(style[window_style_bits::hide_mouse]);
            set_trap_mouse(style[window_style_bits::trap_mouse]);
        }

        void pump_events()
        {
            MSG msg;
            while (PeekMessage(&msg, m_hwnd, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        std::vector<any_event> poll_events()
        {
            return std::move(m_events);
        }

        native_handle_t get_platform_handle() const { return m_hwnd; }

        LRESULT _wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
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
                        keycode = MapVirtualKeyEx(scancode, MAPVK_VSC_TO_VK_EX, m_layout);
                        break;
                    }

                    m_events.emplace_back(key_event{ is_key_down, static_cast<int>(keycode), static_cast<int>(lparam & 0xFFFF) });
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

                    m_events.emplace_back(mouse_click_event{ is_mouse_down, button, point2i(x_pos, y_pos) });
                    break;
                }

                case WM_CLOSE:
                    m_closing = true;
                    break;

                case WM_DESTROY:
                    PostQuitMessage(0);
                    break;

                case WM_ACTIVATE:
                    if (wparam == WA_INACTIVE)
                    {
                        if (m_style & window_style_bits::trap_mouse)
                            ClipCursor(nullptr);
                    }
                    else
                    {
                        if (m_style & window_style_bits::trap_mouse)
                            details::trap_mouse(m_hwnd);
                    }
                    break;

                case WM_MOUSEMOVE:
                {
                    point2i mouse_position(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                    m_events.emplace_back(mouse_move_event{ mouse_position });
                    break;
                }

                case WM_MOUSEWHEEL:
                {
                    UINT lines_per_tick = 0;
                    SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_tick, 0);

                    m_scroll_amount += GET_WHEEL_DELTA_WPARAM(wparam);

                    bool is_negative = m_scroll_amount < 0;
                    int scroll_ticks = 0;
                    while (std::abs(m_scroll_amount) >= WHEEL_DELTA)
                    {
                        scroll_ticks += is_negative ? -1 : 1;
                        m_scroll_amount += is_negative ? WHEEL_DELTA : -WHEEL_DELTA;
                    }

                    if (scroll_ticks != 0)
                    {
                        point2i mouse_position(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                        m_events.emplace_back(mouse_scroll_event{ mouse_position, scroll_ticks * static_cast<int>(lines_per_tick) });
                    }
                    break;
                }

                case WM_SIZE:
                {
                    if (!m_resizing)
                        m_resizing = true;
                    break;
                }

                case WM_EXITSIZEMOVE:
                {
                    if (m_resizing)
                    {
                        RECT rect;
                        GetWindowRect(hwnd, &rect);

                        RECT client_rect;
                        GetClientRect(hwnd, &client_rect);

                        size2u size(static_cast<unsigned>(rect.right - rect.left), static_cast<unsigned>(rect.bottom - rect.top));
                        size2u client_size(static_cast<unsigned>(client_rect.right - client_rect.left), static_cast<unsigned>(client_rect.bottom - client_rect.top));

                        m_events.emplace_back(resize_event{ size, client_size });
                        m_resizing = false;
                    }
                    break;
                }
            }
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }

    private:
        HKL m_layout;
        HWND m_hwnd;
        bool m_closing;
        bool m_resizing;
        int m_scroll_amount;
        flagset<window_style_bits> m_style;
        std::vector<any_event> m_events;
    };

    namespace details
    {
        static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
            static std::unordered_map<HWND, window*> window_map;
            window* ptr = nullptr;

            if (msg == WM_CREATE)
            {
                CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
                ptr = reinterpret_cast<window*>(cs->lpCreateParams);
                window_map[hwnd] = ptr;
            }
            else
            {
                ptr = window_map[hwnd];
            }

            return ptr->_wndproc(hwnd, msg, wparam, lparam);
        }
    }


}