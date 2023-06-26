#include <iostream>

#include <accel/window>

using namespace accel;

int main(int argc, char* argv[])
{
    window_create_params params{};
    params.title = u8"日本語";
    params.style = window_style_bits::resizable;
    params.width = 800u;
    params.height = 600u;
    window wnd(params);

    while (!wnd.is_closing())
    {
        wnd.pump_events();
        auto events = wnd.poll_events();
        for (const auto& event : events)
        {
            switch (event.type)
            {
                case event_types::resize:
                    std::cout << "Client size: " << event.resize.client_width << ", " << event.resize.client_height << "\n";
                    break;
                
                case event_types::mouse_scroll:
                    std::cout << "Scroll: " << event.mouse_scroll.scroll_lines << "\n";
                    break;
            }
        }
    }
    
    return 0;
}