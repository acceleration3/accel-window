#include <iostream>

#include <accel/window.hpp>

using namespace accel;

int main(int argc, char* argv[])
{
    window_create_params params;
    params.title = u8"日本語";
    params.style = window_style_bits::trap_mouse;

    window wnd(params);
    wnd.set_client_size({ 800u, 600u });

    window better_wnd(std::move(wnd));

    while (!better_wnd.closing())
    {
        better_wnd.pump_events();
        auto events = better_wnd.poll_events();
        for (const auto& event : events)
        {
            struct visitor
            {
                void operator()(const mouse_click_event& ev){}
                void operator()(const mouse_move_event& ev){}
                void operator()(const key_event& ev) {}
                void operator()(const resize_event& ev) 
                {
                    std::cout << "Client size: " << ev.client_size.width() << ", " << ev.client_size.height() << "\n";
                }
                void operator()(const mouse_scroll_event& ev)
                {
                    std::cout << "Scroll: " << ev.scroll_lines << "\n";
                }
            };
            std::visit(visitor{}, event);
        }
    }

    return 0;
}