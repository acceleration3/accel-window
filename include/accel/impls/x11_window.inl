#include <array>
#include <cstring>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

namespace accel
{
    using native_handle_t = std::pair<Display*, Window>;

    class window
    {
    public:
        window(const window_create_params& params) : 
            m_closing(false),
            m_display(nullptr)
        {
            m_display = XOpenDisplay(nullptr);
            if (!m_display)
                throw std::runtime_error("Failed to open X display.");
            
            int screen = DefaultScreen(m_display);
            int root_window = DefaultRootWindow(m_display);
            int foreground_color = WhitePixel(m_display, screen);
            int background_color = BlackPixel(m_display, screen);
            m_window = XCreateSimpleWindow(m_display, root_window, 0, 0, params.client_width, params.client_height, 0, foreground_color, background_color);
            
            m_event_mask = ExposureMask | PropertyChangeMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
            XSelectInput(m_display, m_window, m_event_mask);

            m_frame_atom = XInternAtom(m_display, "_NET_FRAME_EXTENTS", False);
            m_close_atom = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
            m_hints_atom = XInternAtom(m_display, "_MOTIF_WM_HINTS", False);

            XSetWMProtocols(m_display, m_window, &m_close_atom, True);

            set_title(params.title);
            set_style(params.style);
        }

        ~window()
        {
            if (m_window)
                XDestroyWindow(m_display, m_window);

            if (m_display)
                XCloseDisplay(m_display);
        }

        window(const window&) = delete;
        window& operator=(const window&) = delete;

        window(window&&) = default;
        window& operator=(window&&) = default;

        unsigned int get_client_width() const
        {
            XWindowAttributes attributes;
            XGetWindowAttributes(m_display, m_window, &attributes);
            return attributes.width;
        }

        unsigned int get_client_height() const
        {
            XWindowAttributes attributes;
            XGetWindowAttributes(m_display, m_window, &attributes);
            return attributes.height;
        }

        unsigned int get_width() const
        {
            std::array<long, 4> frame;
            if (get_frame(frame))
                return get_client_width() + frame[0] + frame[1];
            else
                return get_client_width();
        }

        unsigned int get_height() const
        {
            std::array<long, 4> frame;
            if (get_frame(frame))
                return get_client_height() + frame[2] + frame[3];
            else
                return get_client_height();
        }

        int get_x() const
        {
            XWindowAttributes attributes;
            XGetWindowAttributes(m_display, m_window, &attributes);
            return attributes.x;
        }

        int get_y() const
        {
            XWindowAttributes attributes;
            XGetWindowAttributes(m_display, m_window, &attributes);
            return attributes.y;
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
            XTextProperty text_property;
            XGetWMName(m_display, m_window, &text_property);
            utf8::string utf8_title(reinterpret_cast<char*>(text_property.value));
            XFree(text_property.value);
            return utf8_title;
        }

        void set_title(const utf8::string& title)
        {
            char* strings[1] = { const_cast<char*>(title.data()) };
            XTextProperty text_property;
            if (Xutf8TextListToTextProperty(m_display, strings, 1, XUTF8StringStyle, &text_property) != Success)
                throw std::runtime_error("Failed to create text property.");
            XSetWMName(m_display, m_window, &text_property);
            XFree(text_property.value);
            XFlush(m_display);
        }

        void set_position(int x, int y)
        {
            XMoveWindow(m_display, m_window, x, y);
            XFlush(m_display);
        }

        void set_size(unsigned int width, unsigned int height)
        {
            std::array<long, 4> frame;
            get_frame(frame);
            
            unsigned int new_width = width - (frame[0] + frame[1]);
            unsigned int new_height = height - (frame[2] + frame[3]);
            XResizeWindow(m_display, m_window, new_width, new_height);
            XFlush(m_display);
        }

        void set_client_size(unsigned int width, unsigned int height)
        {
            XResizeWindow(m_display, m_window, width, height);
            XFlush(m_display);
        }

        void set_rect(int x, int y, unsigned int width, unsigned int height)
        {
            set_position(x, y);
            set_size(width, height);
        }

        void set_resizable(bool state)
        {
            XSizeHints* sizeHints = XAllocSizeHints();

            if (!state)
            {
                unsigned int width = get_client_width();
                unsigned int height = get_client_height();

                sizeHints->flags = PMinSize | PMaxSize;
                sizeHints->min_width = width;
                sizeHints->min_height = height;
                sizeHints->max_width = width;
                sizeHints->max_height = height;
            }
            
            XSetWMNormalHints(m_display, m_window, sizeHints);
            
            XFree(sizeHints);

            XFlush(m_display);

            m_style.set(window_style_bits::resizable, state);
        }

        void set_undecorated(bool state)
        {
            struct 
            {
                unsigned long flags;
                unsigned long functions;
                unsigned long decorations;
                long inputMode;
                unsigned long status;
            } hints;

            hints.flags = 2;
            hints.decorations = state ? 0 : 1;

            XChangeProperty(m_display, m_window, m_hints_atom, m_hints_atom, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&hints), 5);
            XFlush(m_display);
            
            m_style.set(window_style_bits::undecorated, state);
        }

        void set_hidden(bool state)
        {
            if (state)
                XUnmapWindow(m_display, m_window);
            else
                XMapWindow(m_display, m_window);
            
            XFlush(m_display);

            m_style.set(window_style_bits::hidden, state);
        }

        void set_hide_mouse(bool state)
        {
            if (state)
                XDefineCursor(m_display, m_window, None);
            else
                XUndefineCursor(m_display, m_window);

            XFlush(m_display);

            m_style.set(window_style_bits::hide_mouse, state);
        }

        void set_trap_mouse(bool state)
        {
            if (state)
                XGrabPointer(m_display, m_window, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, m_window, None, CurrentTime);
            else
                XUngrabPointer(m_display, CurrentTime);

            XFlush(m_display);

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
            XEvent event;

            // Close detection first to avoid processing unnecessary events
            if (XCheckTypedWindowEvent(m_display, m_window, ClientMessage, &event))
            {
                if (event.xclient.data.l[0] == m_close_atom)
                    m_closing = true;
            }

            std::vector<generic_event> polled_events;   
            while (XCheckWindowEvent(m_display, m_window, m_event_mask, &event)) 
            {    
                switch (event.type)
                {
                    case KeyPress:
                        polled_events.emplace_back(key_down_event{ event.xkey.keycode });
                        break;
                    
                    case KeyRelease:
                        polled_events.emplace_back(key_up_event{ event.xkey.keycode });
                        break;

                    case ButtonPress:
                        if (event.xbutton.button < 4)
                            polled_events.emplace_back(mouse_down_event{ static_cast<mouse_buttons>(event.xbutton.button), event.xbutton.x, event.xbutton.y });
                        else if (event.xbutton.button == 4)
                            polled_events.emplace_back(mouse_scroll_event{ event.xbutton.x, event.xbutton.y, mouse_scroll_directions::up });
                        else if (event.xbutton.button == 5)
                            polled_events.emplace_back(mouse_scroll_event{ event.xbutton.x, event.xbutton.y, mouse_scroll_directions::down });
                        break;

                    case ButtonRelease:
                        polled_events.emplace_back(mouse_up_event{ static_cast<mouse_buttons>(event.xbutton.button), event.xbutton.x, event.xbutton.y });
                        break;

                    case MotionNotify:
                        polled_events.emplace_back(mouse_move_event{ event.xmotion.x, event.xmotion.y });
                        break;

                    case ConfigureNotify:
                        if (event.xconfigure.width != m_prev_width || event.xconfigure.height != m_prev_height)
                        {
                            std::array<long, 4> frame;
                            get_frame(frame);

                            unsigned int client_width = static_cast<unsigned int>(event.xconfigure.width + frame[0] + frame[1]);
                            unsigned int client_height = static_cast<unsigned int>(frame[2] + frame[3]);
                            unsigned int width = static_cast<unsigned int>(event.xconfigure.width);
                            unsigned int height = static_cast<unsigned int>(event.xconfigure.height);

                            polled_events.emplace_back(resize_event{ client_width, client_height, width, height });
                        }
                        break;
                }
            }

            std::copy(polled_events.begin(), polled_events.end(), position_it);
        }

        native_handle_t get_platform_handle() const { return std::make_pair(m_display, m_window); }

    private:
        Display* m_display;
        Window m_window;

        Atom m_close_atom;
        Atom m_frame_atom;
        Atom m_hints_atom;

        long m_event_mask;
        bool m_closing;
        flagset<window_style_bits> m_style;

        unsigned int m_prev_width;
        unsigned int m_prev_height;

        bool get_frame(std::array<long, 4>& values) const
        {
            Atom actual_type;
            int actual_format;
            unsigned long item_count, bytes_after;
            unsigned char* prop_value = nullptr;
            if (!XGetWindowProperty(m_display, m_window, m_frame_atom, 0, 4, False, AnyPropertyType, &actual_type, &actual_format, &item_count, &bytes_after, &prop_value) == Success)
                return false;
            
            if (actual_type != XA_CARDINAL || actual_format != 32 || item_count != 4)
            {
                XFree(prop_value);
                return false;
            }

            std::memcpy(reinterpret_cast<void*>(&values[0]), reinterpret_cast<const void*>(prop_value), sizeof(long) * 4);

            XFree(prop_value);
            return true;
        }
    };
}