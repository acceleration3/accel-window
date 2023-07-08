#include <iostream>
#include <thread>


#include <accel/window>

using namespace accel;

int main(int argc, char* argv[])
{
    window_create_params params{};
    params.title = u8"日本語";
    params.style = window_style_bits::resizable;
    params.client_width = 800u;
    params.client_height = 600u;
    window wnd(params);

    while (!wnd.is_closing())
    {
        std::vector<generic_event> events;
        wnd.poll_events(std::back_inserter(events));

        for (const auto& event : events)
        {
            switch (event.type)
            {
                case event_types::resize:
                    std::cout << "Client size: " << event.resize.client_width << ", " << event.resize.client_height << "\n";
                    break;
                
                case event_types::mouse_scroll:
                    std::string dirs[2] = { "up", "down" };
                    std::cout << "Scroll: " << dirs[static_cast<int>(event.mouse_scroll.direction)] << "\n";
                    break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    return 0;
}