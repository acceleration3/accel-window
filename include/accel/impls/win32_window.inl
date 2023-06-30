#include <unordered_map>
#include <algorithm>

#define UNICODE
#define WINDOW_CLASS L"AccelWindow"

#include <Windows.h>
#include <windowsx.h>

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
            static api_call get_rect = reinterpret_cast<api_call>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmGetWindowAttribute"));
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
    }

    class window
    {
    public:
        window(const window_create_params& params) : 
            m_closing(false),
            m_hwnd(nullptr),
            m_scroll_amount(0)
        {
            static HINSTANCE hinstance = GetModuleHandleW(nullptr);
            static bool initialized = false;
            if (!initialized)
            {
                WNDCLASSW cls{};
                cls.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
                cls.lpszClassName = WINDOW_CLASS;
                cls.lpfnWndProc = &details::wndproc;
                cls.hInstance = hinstance;
                cls.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
                cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
                if (!RegisterClassW(&cls))
                    throw std::runtime_error("Failed to register window class.");

                initialized = true;
            }

            m_hwnd = CreateWindowW(WINDOW_CLASS, params.title.to_wstring().c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, params.client_width, params.client_height, nullptr, nullptr, hinstance, reinterpret_cast<LPVOID>(this));
            if (!m_hwnd)
                throw std::runtime_error("Failed to create window.");

            set_style(params.style);
            set_client_size(params.client_width, params.client_height);
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

        unsigned int get_client_width() const
        {
            RECT rect;
            GetClientRect(m_hwnd, &rect);
            return static_cast<unsigned int>(rect.right - rect.left);
        }

        unsigned int get_client_height() const
        {
            RECT rect;
            GetClientRect(m_hwnd, &rect);
            return static_cast<unsigned int>(rect.bottom - rect.top);
        }

        unsigned int get_width() const
        {
            RECT rect = details::get_real_rect(m_hwnd);
            return static_cast<unsigned int>((std::max)(rect.right - rect.left, LONG(0)));
        }

        unsigned int get_height() const
        {
            RECT rect = details::get_real_rect(m_hwnd);
            return static_cast<unsigned int>((std::max)(rect.bottom - rect.top, LONG(0)));
        }

        int get_x() const
        {
            RECT rect = details::get_real_rect(m_hwnd);
            return rect.left;
        }

        int get_y() const
        {
            RECT rect = details::get_real_rect(m_hwnd);
            return rect.left;
        }

        bool is_closing() const { return m_closing; }
        bool is_resizable() const { return m_style[window_style_bits::resizable]; }
        bool is_undecorated() const { return m_style[window_style_bits::undecorated]; }
        bool is_hidden() const { return m_style[window_style_bits::undecorated]; }
        bool is_hiding_mouse() const { return m_style[window_style_bits::hide_mouse]; }
        bool is_trapping_mouse() const { return m_style[window_style_bits::trap_mouse]; }
        flagset<window_style_bits> get_style() const { return m_style; }
        
        utf8::string get_title() const
        {
            int size = GetWindowTextLengthW(m_hwnd) + 1;
            std::wstring title(size, '\0');
            GetWindowTextW(m_hwnd, &title[0], size);
            return utf8::string(title);
        }

        void set_title(const utf8::string& title)
        {
            SetWindowTextW(m_hwnd, title.to_wstring().c_str());
        }

        void set_position(int x, int y)
        {
            RECT excluding_shadow = details::get_real_rect(m_hwnd);

            RECT including_shadow = {};
            GetWindowRect(m_hwnd, &including_shadow);

            int shadow_left = including_shadow.left - excluding_shadow.left;
            int shadow_top = including_shadow.top - excluding_shadow.top;
            SetWindowPos(m_hwnd, 0, x + shadow_left, y + shadow_top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

            if (m_style & window_style_bits::trap_mouse)
                details::trap_mouse(m_hwnd);
        }

        void set_size(unsigned int width, unsigned int height)
        {
            RECT excluding_shadow = details::get_real_rect(m_hwnd);

            RECT including_shadow;
            GetWindowRect(m_hwnd, &including_shadow);

            int shadow_width = (including_shadow.right - excluding_shadow.right) + (excluding_shadow.left - including_shadow.left);
            int shadow_height = (including_shadow.bottom - excluding_shadow.bottom) + (excluding_shadow.top - including_shadow.top);
            SetWindowPos(m_hwnd, 0, 0, 0, width + shadow_width, height + shadow_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

            if (m_style & window_style_bits::trap_mouse)
                details::trap_mouse(m_hwnd);
        }

        void set_client_size(unsigned int width, unsigned int height)
        {
            RECT rect{};
            rect.right = width;
            rect.bottom = height;

            DWORD style = GetWindowLong(m_hwnd, GWL_STYLE);
            AdjustWindowRect(&rect, style, FALSE);

            SetWindowPos(m_hwnd, 0, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

            if (m_style & window_style_bits::trap_mouse)
                details::trap_mouse(m_hwnd);
        }

        void set_rect(int x, int y, unsigned int width, unsigned int height)
        {
            set_position(x, y);
            set_size(width, height);
        }

        void set_resizable(bool state)
        {
            LONG_PTR style = GetWindowLongPtrW(m_hwnd, GWL_STYLE);

            if (state)
                style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
            else
                style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

            SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);
            m_style.set(window_style_bits::resizable, state);
        }

        void set_undecorated(bool state)
        {
            LONG_PTR style = GetWindowLongPtrW(m_hwnd, GWL_STYLE);

            if (state)
                style &= ~(WS_CAPTION | WS_MINIMIZEBOX);
            else
                style |= WS_CAPTION | WS_MINIMIZEBOX;

            SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);
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

        template<typename ItT>
        void poll_events(ItT position_it)
        {
            MSG msg;
            while (PeekMessageW(&msg, m_hwnd, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            std::copy(m_events.cbegin(), m_events.cend(), position_it);
            
            m_events.clear();
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
                        keycode = MapVirtualKeyExW(scancode, MAPVK_VSC_TO_VK_EX, m_layout);
                        break;
                    }

                    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
                        m_events.emplace_back(key_down_event{ static_cast<unsigned int>(keycode) });
                    else
                        m_events.emplace_back(key_up_event{ static_cast<unsigned int>(keycode) });
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

                    if (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_XBUTTONDOWN)
                        m_events.emplace_back(mouse_down_event{ button, x_pos, y_pos });
                    else
                        m_events.emplace_back(mouse_up_event{ button, x_pos, y_pos });
                    
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
                    int mouse_x = GET_X_LPARAM(lparam);
                    int mouse_y = GET_Y_LPARAM(lparam);
                    m_events.emplace_back(mouse_move_event{ mouse_x, mouse_y });
                    break;
                }

                case WM_MOUSEWHEEL:
                {
                    UINT lines_per_tick = 0;
                    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines_per_tick, 0);

                    m_scroll_amount += GET_WHEEL_DELTA_WPARAM(wparam);

                    int mouse_x = GET_X_LPARAM(lparam);
                    int mouse_y = GET_Y_LPARAM(lparam);

                    bool is_negative = m_scroll_amount < 0;
                    while (std::abs(m_scroll_amount) >= WHEEL_DELTA)
                    {
                        m_events.emplace_back(mouse_scroll_event{ mouse_x, mouse_y, is_negative ? mouse_scroll_directions::down : mouse_scroll_directions::up });
                        m_scroll_amount += is_negative ? WHEEL_DELTA : -WHEEL_DELTA;
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

                        unsigned int width = static_cast<unsigned>(rect.right - rect.left);
                        unsigned int height = static_cast<unsigned>(rect.bottom - rect.top);
                        unsigned int client_width = static_cast<unsigned>(client_rect.right - client_rect.left);
                        unsigned int client_height = static_cast<unsigned>(client_rect.bottom - client_rect.top);

                        m_events.emplace_back(resize_event{ width, height, client_width, client_height });
                        m_resizing = false;
                    }
                    break;
                }
            }

            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

    private:
        HKL m_layout;
        HWND m_hwnd;
        bool m_closing;
        bool m_resizing;
        int m_scroll_amount;
        flagset<window_style_bits> m_style;
        std::vector<generic_event> m_events;
    };

    namespace details
    {
        static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
            accel::window* ptr = nullptr;

            if (msg == WM_CREATE)
            {
                CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
                ptr = reinterpret_cast<accel::window*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ptr));
            }
            else
            {
                ptr = reinterpret_cast<accel::window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            }

            if (ptr)
                return ptr->_wndproc(hwnd, msg, wparam, lparam);
            else
                return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }
}