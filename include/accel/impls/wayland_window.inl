#include <algorithm>
#include <functional>
#include <memory>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include <xdg-shell.h>
#include <xdg-decoration.h>

namespace accel
{
    namespace details
    {
        // Listener callback declarations
        static void registry_global(void* data, wl_registry* wl_registry, std::uint32_t name, const char* interface, std::uint32_t version);
        static void wm_base_ping(void* data, xdg_wm_base* xdg_wm_base, std::uint32_t serial);
        static void surface_configure(void* data, xdg_surface* xdg_surface, std::uint32_t serial);
        static void tl_configure(void* data, xdg_toplevel* xdg_toplevel, std::int32_t width, std::int32_t height, wl_array *states);
        static void tl_close(void* data, xdg_toplevel* xdg_toplevel);
        static void tl_configure_bounds(void* data, xdg_toplevel* xdg_toplevel, std::int32_t width, std::int32_t height);
        static void tl_wm_capabilities(void* data, xdg_toplevel* xdg_toplevel, wl_array* capabilities);


        // Definitions for simple callbacks 
        static void wl_buffer_release(void* data, wl_buffer* wl_buffer)
        {
            wl_buffer_destroy(wl_buffer);
        }

        static void wm_base_ping(void* data, xdg_wm_base* xdg_wm_base, std::uint32_t serial)
        {
            xdg_wm_base_pong(xdg_wm_base, serial);
        }

        static void registry_global_remove(void* data, wl_registry* registry, std::uint32_t name) {}


        // Listeners
        static wl_registry_listener registry_listener { &registry_global, &registry_global_remove };

        static wl_buffer_listener buffer_listener { &wl_buffer_release };
        static xdg_wm_base_listener wm_base_listener { &wm_base_ping };
        static xdg_surface_listener surface_listener { &surface_configure };
        static xdg_toplevel_listener toplevel_listener { &tl_configure, &tl_close, &tl_configure_bounds, &tl_wm_capabilities };

        struct shared_memory
        {
            std::string name;
            wl_shm_pool* pool;

            shared_memory(wl_shm* shm, std::size_t size) :
                pool(nullptr)
            {
                name = get_dir() + "/wl-shm-XXXXXX";

                int fd = mkostemp(&name[0], O_CLOEXEC);
                if (fd < 0)
                    throw std::runtime_error("Failed to create shared memory file.");

                unlink(name.c_str());

                if (ftruncate(fd, size) < 0)
                    throw std::runtime_error("Failed to allocate file size.");

                pool = wl_shm_create_pool(shm, fd, size);
                if (!pool)
                    throw std::runtime_error("Failed to create pool for shared memory.");
                
                close(fd);
            }

            ~shared_memory()
            {
                if (pool)
                    wl_shm_pool_destroy(pool);
            }

        private:
            static std::string get_dir() 
            {
                std::string dir = getenv("XDG_RUNTIME_DIR");
                return dir.empty() ? getenv("TMPDIR") : dir;
            }
        };

        struct wayland_state
        {
            // Objects
            std::unique_ptr<shared_memory> memory;
            wl_surface* surf;
            xdg_surface* xdg_surf;
            xdg_toplevel* top_level;
            zxdg_toplevel_decoration_v1* decoration;
            
            // Globals
            wl_display* display;
            wl_registry* registry;
            wl_compositor* compositor;
            wl_seat* seat;
            wl_shm* shm;
            xdg_wm_base* wm_base;
            zxdg_decoration_manager_v1* decoration_manager;

            // Callbacks
            std::function<void()> configure;

            // Variables
            bool is_closing;
            bool seen_first_config;
            std::int32_t width;
            std::int32_t height;

            wayland_state(std::int32_t width, std::int32_t height) :
                display(nullptr),
                registry(nullptr),
                compositor(nullptr),
                seat(nullptr),
                shm(nullptr),
                wm_base(nullptr),
                decoration_manager(nullptr),
                surf(nullptr),
                xdg_surf(nullptr),
                top_level(nullptr),
                decoration(nullptr),
                is_closing(false),
                seen_first_config(false),
                width(width),
                height(height)
            {
                display = wl_display_connect(nullptr);
                if (!display)
                    throw std::runtime_error("Failed to connect to Wayland display.");
                
                registry = wl_display_get_registry(display);
                if (!registry)
                    throw std::runtime_error("Failed to get registry from Wayland display.");

                wl_registry_add_listener(registry, &registry_listener, this);
                wl_display_roundtrip(display);

                if (!compositor || !seat || !shm || !decoration_manager || !wm_base)
                    throw std::runtime_error("Failed to bind all the necessary globals from the registry.");
            
                surf = wl_compositor_create_surface(compositor);
                if (!surf)
                    throw std::runtime_error("Compositor failed to create surface.");

                xdg_surf = xdg_wm_base_get_xdg_surface(wm_base, surf);
                if (!xdg_surf)
                    throw std::runtime_error("Failed to get XDG surface.");

                xdg_surface_add_listener(xdg_surf, &surface_listener, this);

                top_level = xdg_surface_get_toplevel(xdg_surf);
                if (!top_level)
                    throw std::runtime_error("Failed to get XDG surface top level.");

                xdg_toplevel_add_listener(top_level, &toplevel_listener, this);

                decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, top_level);
                if (!decoration)
                    throw std::runtime_error("Failed to get XDG top level decoration.");

                resize_surface(width, height);

                while (!seen_first_config) 
                    wl_display_dispatch(display);
            }

            ~wayland_state()
            {
                memory.reset();
                xdg_toplevel_destroy(top_level);
                xdg_surface_destroy(xdg_surf);
                wl_surface_destroy(surf);
                zxdg_decoration_manager_v1_destroy(decoration_manager);
                xdg_wm_base_destroy(wm_base);
                wl_shm_destroy(shm);
                wl_seat_release(seat);
                wl_compositor_destroy(compositor);
                wl_registry_destroy(registry);
                wl_display_disconnect(display);
            }
        
            void resize_surface(std::int32_t new_width, std::int32_t new_height)
            {
                std::size_t stride = new_width * 4;
                std::size_t size = stride * new_height;

                memory.reset(new shared_memory(shm, size));

                wl_buffer* buffer = wl_shm_pool_create_buffer(memory->pool, 0, new_width, new_height, stride, WL_SHM_FORMAT_XRGB8888);
                if (!buffer)
                    throw std::runtime_error("Failed to create buffer from pool.");
                wl_buffer_add_listener(buffer, &details::buffer_listener, NULL);

                wl_surface_attach(surf, buffer, 0, 0);
                wl_surface_commit(surf);

                width = new_width;
                height = new_height;
            }

        };
    
        
        // Definitions for listener callbacks that need state info
        static void registry_global(void* data, wl_registry* wl_registry, std::uint32_t name, const char* interface, std::uint32_t version)
        {
            auto state = static_cast<wayland_state*>(data);
            std::string interface_name(interface);

            if (interface_name == wl_compositor_interface.name)
            {
                state->compositor = static_cast<wl_compositor*>(wl_registry_bind(wl_registry, name, &wl_compositor_interface, version));
            }
            else if (interface_name == wl_seat_interface.name)
            {
                state->seat = static_cast<wl_seat*>(wl_registry_bind(wl_registry, name, &wl_seat_interface, version));
            }
            else if (interface_name == wl_shm_interface.name)
            {
                state->shm = static_cast<wl_shm*>(wl_registry_bind(wl_registry, name, &wl_shm_interface, version));
            }
            else if (interface_name == xdg_wm_base_interface.name)
            {
                state->wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, version));
                xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, nullptr);
            }
            else if (interface_name == zxdg_decoration_manager_v1_interface.name)
            {
                state->decoration_manager = static_cast<zxdg_decoration_manager_v1*>(wl_registry_bind(wl_registry, name, &zxdg_decoration_manager_v1_interface, version));
            }
        }
    
        static void surface_configure(void* data, xdg_surface* xdg_surface, std::uint32_t serial)
        {
            auto state = static_cast<wayland_state*>(data);

            if (!state->seen_first_config)
                state->seen_first_config = true;

            xdg_surface_ack_configure(xdg_surface, serial);
        }

        static void tl_configure(void* data, xdg_toplevel* xdg_toplevel, std::int32_t width, std::int32_t height, wl_array *states)
        {
            if (width <= 0 || height <= 0)
                return;
            
            auto state = static_cast<wayland_state*>(data);
            state->resize_surface(width, height);
        }

        static void tl_close(void* data, xdg_toplevel* xdg_toplevel)
        {
            auto state = static_cast<wayland_state*>(data);
            state->is_closing = true;
        }

        static void tl_configure_bounds(void* data, xdg_toplevel* xdg_toplevel, std::int32_t width, std::int32_t height)
        {
        }

        static void tl_wm_capabilities(void* data, xdg_toplevel* xdg_toplevel, wl_array* capabilities)
        {
        }
    }

    using native_handle_t = details::wayland_state&;

    class window
    {
    public:
        window(const window_create_params& params) :
            m_state(params.client_width, params.client_height)
        {
            zxdg_toplevel_decoration_v1_set_mode(m_state.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            xdg_toplevel_set_title(m_state.top_level, params.title.data());

            wl_surface_commit(m_state.surf);
        }

        bool is_closing() const
        {
            return m_state.is_closing;
        }

        template<typename ItT>
        void poll_events(ItT position_it)
        {
            while (wl_display_prepare_read(m_state.display) != 0)
                wl_display_dispatch_pending(m_state.display);

            wl_display_flush(m_state.display);
            wl_display_read_events(m_state.display);
        }

        native_handle_t get_platform_handle() const { return m_state; }

    private:
        mutable details::wayland_state m_state;
        wl_buffer* m_buffer;
    };
}