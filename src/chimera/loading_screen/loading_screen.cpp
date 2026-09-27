// SPDX-License-Identifier: GPL-3.0-only

// This is the loading screen from Balltze (https://github.com/Kavawuvi/balltze), adapted for Chimera.
//
// The shader is based on a GLSL shader from SnowyMouse: https://gist.github.com/SnowyMouse/eb7558d2e036e4becbc764501d99a703

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <windows.h>
#include <d3d9.h>

#include "../chimera.hpp"
#include "../config/ini.hpp"
#include "../event/d3d9_end_scene.hpp"
#include "../halo_data/shaders/shader_blob.hpp"
#include "../output/output.hpp"
#include "../signature/hook.hpp"
#include "../signature/signature.hpp"

#include "loading_screen.hpp"

// GDI+ is picky about what has been included before it (it wants min and max), so keep this last
#include <gdiplus.h>

using namespace std::chrono_literals;

extern "C" {
    // The PNG is embedded by loading_screen_background.S
    extern const std::uint8_t loading_screen_background_png[];
    extern const std::uint8_t loading_screen_background_png_end[];
}

namespace Chimera {
    using Clock = std::chrono::steady_clock;

    /** Length of the shader animation, in the same units as the shader's iDuration */
    static constexpr auto SHADER_EFFECT_DURATION = 4000ms;

    /** The demo runs for one full sweep of the animation (1.5 times the duration), plus a little bit extra */
    static constexpr auto DEMO_DURATION = SHADER_EFFECT_DURATION * 8 / 5;

    /** The main menu map. Loading this one is quick, so we don't bother with the loading screen. */
    static constexpr const char *UI_MAP_NAME = "levels\\ui\\ui";

    /** Value of Halo's loading screen flag when loading into a multiplayer game */
    static constexpr std::uint32_t LOADING_SCREEN_FLAG_MULTIPLAYER = 8;

    namespace {
        /** Vertex used for the full-screen quad (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1) */
        struct Vertex {
            float x;
            float y;
            float z;
            float rhw;
            DWORD color;
            float u;
            float v;
        };
        static_assert(sizeof(Vertex) == 28);
    }

    static constexpr DWORD VERTEX_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    extern "C" {
        /** Address of Halo's load map function that we can call, set when the load map function is overridden */
        const void *load_map_function_address = nullptr;

        std::uint32_t load_map_worker_asm(const char *map_name) noexcept;
        void load_map_override_asm() noexcept;
        void set_d3d9_device_multithreaded_flag() noexcept;
        void loading_screen_background_render_asm() noexcept;
    }

    /** True once the loading screen is fully set up */
    static bool loading_screen_enabled = false;

    /** True if chimera_block_loading_screen is on */
    static bool loading_screen_blocked = false;

    /** True while the loading screen is being drawn */
    static bool loading_screen_playback = false;

    /** True if the loading screen is being drawn as a demo (chimera_test_loading_screen) rather than while loading */
    static bool loading_screen_demo = false;

    static std::optional<Clock::time_point> loading_screen_start_time;

    /** Alpha of Halo's own loading screen background, which we follow so we fade out when it does */
    static std::uint8_t loading_screen_alpha = 255;

    /** Halo's loading screen flag; this is nonzero when Halo wants to show the loading screen */
    static const volatile std::uint32_t *game_loading_screen_flag = nullptr;

    /** Halo's function that draws its loading screen */
    static void (*game_loading_screen_function)() = nullptr;

    static LPDIRECT3DDEVICE9 device = nullptr;
    static LPDIRECT3DTEXTURE9 background_texture = nullptr;
    static LPDIRECT3DPIXELSHADER9 background_shader = nullptr;
    static bool resources_failed = false;

    static bool resources_ready() noexcept {
        return device && background_texture && background_shader;
    }

    static void release_resources() noexcept {
        if(background_texture) {
            background_texture->Release();
            background_texture = nullptr;
        }
        if(background_shader) {
            background_shader->Release();
            background_shader = nullptr;
        }
        resources_failed = false;
    }

    /**
     * Decode the embedded PNG into a texture.
     * @param  dev     device to create the texture on
     * @param  texture pointer to where to put the texture
     * @return         true on success
     */
    static bool create_background_texture(LPDIRECT3DDEVICE9 dev, LPDIRECT3DTEXTURE9 *texture) noexcept {
        auto png_size = static_cast<SIZE_T>(reinterpret_cast<std::uintptr_t>(loading_screen_background_png_end) - reinterpret_cast<std::uintptr_t>(loading_screen_background_png));

        // GDI+ can only read from a stream, so copy the PNG into a block of memory that the stream will take ownership of
        HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, png_size);
        if(!global) {
            return false;
        }
        void *global_data = GlobalLock(global);
        if(!global_data) {
            GlobalFree(global);
            return false;
        }
        std::memcpy(global_data, loading_screen_background_png, png_size);
        GlobalUnlock(global);

        IStream *stream = nullptr;
        if(FAILED(CreateStreamOnHGlobal(global, TRUE, &stream)) || !stream) {
            GlobalFree(global);
            return false;
        }

        Gdiplus::GdiplusStartupInput startup_input;
        ULONG_PTR gdiplus_token = 0;
        if(Gdiplus::GdiplusStartup(&gdiplus_token, &startup_input, nullptr) != Gdiplus::Ok) {
            stream->Release();
            return false;
        }

        bool success = false;
        {
            // The stream has to outlive the bitmap, and the bitmap has to be gone before we shut GDI+ down
            Gdiplus::Bitmap bitmap(stream);
            if(bitmap.GetLastStatus() == Gdiplus::Ok) {
                UINT width = bitmap.GetWidth();
                UINT height = bitmap.GetHeight();

                if(SUCCEEDED(dev->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, texture, nullptr))) {
                    Gdiplus::BitmapData bitmap_data;
                    Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
                    D3DLOCKED_RECT locked_rect;

                    if(bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmap_data) == Gdiplus::Ok) {
                        if(SUCCEEDED((*texture)->LockRect(0, &locked_rect, nullptr, 0))) {
                            auto *source = static_cast<const std::uint8_t *>(bitmap_data.Scan0);
                            auto *destination = static_cast<std::uint8_t *>(locked_rect.pBits);
                            for(UINT y = 0; y < height; y++) {
                                std::memcpy(destination + static_cast<std::ptrdiff_t>(y) * locked_rect.Pitch, source + static_cast<std::ptrdiff_t>(y) * bitmap_data.Stride, static_cast<std::size_t>(width) * 4);
                            }
                            (*texture)->UnlockRect(0);
                            success = true;
                        }
                        bitmap.UnlockBits(&bitmap_data);
                    }

                    if(!success) {
                        (*texture)->Release();
                        *texture = nullptr;
                    }
                }
            }
        }

        stream->Release();
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return success;
    }

    static void create_resources() noexcept {
        if(!device || resources_failed || (background_texture && background_shader)) {
            return;
        }

        release_resources();

        if(!create_background_texture(device, &background_texture)) {
            console_error("Failed to load the loading screen background. The loading screen will not be shown.");
            resources_failed = true;
            return;
        }

        if(FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD *>(loading_screen_shader), &background_shader))) {
            console_error("Failed to create the loading screen shader. The loading screen will not be shown.");
            release_resources();
            resources_failed = true;
        }
    }

    namespace {
    struct RenderStateValue {
        D3DRENDERSTATETYPE state;
        DWORD value;
    };

    struct TextureStageStateValue {
        DWORD stage;
        D3DTEXTURESTAGESTATETYPE state;
        DWORD value;
    };

    struct SamplerStateValue {
        D3DSAMPLERSTATETYPE state;
        DWORD value;
    };

    /** States we need for drawing our quad (they are restored afterwards) */
    constexpr RenderStateValue RENDER_STATES[] = {
        { D3DRS_ALPHABLENDENABLE, TRUE },
        { D3DRS_ALPHAFUNC, D3DCMP_GREATER },
        { D3DRS_ALPHAREF, 0 },
        { D3DRS_ALPHATESTENABLE, TRUE },
        { D3DRS_BLENDOP, D3DBLENDOP_ADD },
        { D3DRS_CLIPPING, TRUE },
        { D3DRS_CLIPPLANEENABLE, 0 },
        { D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED },
        { D3DRS_CULLMODE, D3DCULL_NONE },
        { D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA },
        { D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1 },
        { D3DRS_ENABLEADAPTIVETESSELLATION, FALSE },
        { D3DRS_FILLMODE, D3DFILL_SOLID },
        { D3DRS_FOGENABLE, FALSE },
        { D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE },
        { D3DRS_LIGHTING, FALSE },
        { D3DRS_RANGEFOGENABLE, FALSE },
        { D3DRS_SCISSORTESTENABLE, FALSE },
        { D3DRS_SEPARATEALPHABLENDENABLE, FALSE },
        { D3DRS_SHADEMODE, D3DSHADE_GOURAUD },
        { D3DRS_SPECULARENABLE, FALSE },
        { D3DRS_SRCBLEND, D3DBLEND_SRCALPHA },
        { D3DRS_SRGBWRITEENABLE, FALSE },
        { D3DRS_STENCILENABLE, FALSE },
        { D3DRS_VERTEXBLEND, D3DVBF_DISABLE },
        { D3DRS_WRAP0, 0 },
        { D3DRS_ZENABLE, D3DZB_FALSE },
        { D3DRS_ZWRITEENABLE, FALSE }
    };

    constexpr TextureStageStateValue TEXTURE_STAGE_STATES[] = {
        { 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE },
        { 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE },
        { 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE },
        { 0, D3DTSS_COLORARG1, D3DTA_TEXTURE },
        { 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE },
        { 0, D3DTSS_COLOROP, D3DTOP_MODULATE },
        { 0, D3DTSS_TEXCOORDINDEX, 0 },
        { 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE },
        { 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE },
        { 1, D3DTSS_COLOROP, D3DTOP_DISABLE }
    };

    constexpr SamplerStateValue SAMPLER_STATES[] = {
        { D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP },
        { D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
        { D3DSAMP_MAGFILTER, D3DTEXF_LINEAR },
        { D3DSAMP_MINFILTER, D3DTEXF_LINEAR },
        { D3DSAMP_MIPFILTER, D3DTEXF_NONE },
        { D3DSAMP_MAXMIPLEVEL, 0 },
        { D3DSAMP_MIPMAPLODBIAS, 0 },
        { D3DSAMP_SRGBTEXTURE, 0 }
    };

    /**
     * We draw in the middle of Halo's rendering (or between its frames, while a map is loading), so anything we change
     * about the device has to be put back the way it was.
     */
    class SavedDeviceState {
    public:
        SavedDeviceState(LPDIRECT3DDEVICE9 dev) noexcept : p_device(dev) {
            for(std::size_t i = 0; i < std::size(RENDER_STATES); i++) {
                dev->GetRenderState(RENDER_STATES[i].state, &this->p_render_states[i]);
            }
            for(std::size_t i = 0; i < std::size(TEXTURE_STAGE_STATES); i++) {
                dev->GetTextureStageState(TEXTURE_STAGE_STATES[i].stage, TEXTURE_STAGE_STATES[i].state, &this->p_texture_stage_states[i]);
            }
            for(std::size_t i = 0; i < std::size(SAMPLER_STATES); i++) {
                dev->GetSamplerState(0, SAMPLER_STATES[i].state, &this->p_sampler_states[i]);
            }

            dev->GetPixelShader(&this->p_pixel_shader);
            dev->GetPixelShaderConstantF(0, this->p_pixel_shader_constants, 4);
            dev->GetVertexShader(&this->p_vertex_shader);
            dev->GetVertexDeclaration(&this->p_vertex_declaration);
            dev->GetTexture(0, &this->p_texture);
            dev->GetStreamSource(0, &this->p_stream_data, &this->p_stream_offset, &this->p_stream_stride);
        }

        ~SavedDeviceState() noexcept {
            auto *dev = this->p_device;

            for(std::size_t i = 0; i < std::size(RENDER_STATES); i++) {
                dev->SetRenderState(RENDER_STATES[i].state, this->p_render_states[i]);
            }
            for(std::size_t i = 0; i < std::size(TEXTURE_STAGE_STATES); i++) {
                dev->SetTextureStageState(TEXTURE_STAGE_STATES[i].stage, TEXTURE_STAGE_STATES[i].state, this->p_texture_stage_states[i]);
            }
            for(std::size_t i = 0; i < std::size(SAMPLER_STATES); i++) {
                dev->SetSamplerState(0, SAMPLER_STATES[i].state, this->p_sampler_states[i]);
            }

            dev->SetPixelShader(this->p_pixel_shader);
            dev->SetPixelShaderConstantF(0, this->p_pixel_shader_constants, 4);
            dev->SetVertexShader(this->p_vertex_shader);
            if(this->p_vertex_declaration) {
                dev->SetVertexDeclaration(this->p_vertex_declaration);
            }
            dev->SetTexture(0, this->p_texture);
            if(this->p_stream_data) {
                dev->SetStreamSource(0, this->p_stream_data, this->p_stream_offset, this->p_stream_stride);
            }

            // The Get functions above add a reference to anything they give us
            if(this->p_pixel_shader) {
                this->p_pixel_shader->Release();
            }
            if(this->p_vertex_shader) {
                this->p_vertex_shader->Release();
            }
            if(this->p_vertex_declaration) {
                this->p_vertex_declaration->Release();
            }
            if(this->p_texture) {
                this->p_texture->Release();
            }
            if(this->p_stream_data) {
                this->p_stream_data->Release();
            }
        }

        SavedDeviceState(const SavedDeviceState &) = delete;
        SavedDeviceState &operator=(const SavedDeviceState &) = delete;

    private:
        LPDIRECT3DDEVICE9 p_device;
        DWORD p_render_states[std::size(RENDER_STATES)] = {};
        DWORD p_texture_stage_states[std::size(TEXTURE_STAGE_STATES)] = {};
        DWORD p_sampler_states[std::size(SAMPLER_STATES)] = {};
        float p_pixel_shader_constants[16] = {};
        LPDIRECT3DPIXELSHADER9 p_pixel_shader = nullptr;
        LPDIRECT3DVERTEXSHADER9 p_vertex_shader = nullptr;
        LPDIRECT3DVERTEXDECLARATION9 p_vertex_declaration = nullptr;
        LPDIRECT3DBASETEXTURE9 p_texture = nullptr;
        LPDIRECT3DVERTEXBUFFER9 p_stream_data = nullptr;
        UINT p_stream_offset = 0;
        UINT p_stream_stride = 0;
    };
    }

    static void play_loading_screen_background() noexcept {
        if(loading_screen_playback) {
            return;
        }
        loading_screen_alpha = 255;
        loading_screen_playback = true;
    }

    static void end_loading_screen_background() noexcept {
        if(!loading_screen_playback) {
            return;
        }
        loading_screen_playback = false;
        loading_screen_demo = false;
        loading_screen_start_time = std::nullopt;
    }

    static void draw_loading_screen_background() noexcept {
        if(!loading_screen_playback || !resources_ready()) {
            return;
        }

        if(!loading_screen_start_time) {
            loading_screen_start_time = Clock::now();
        }

        // The shader works in terms of the size of the render target
        LPDIRECT3DSURFACE9 render_target = nullptr;
        if(FAILED(device->GetRenderTarget(0, &render_target)) || !render_target) {
            return;
        }
        D3DSURFACE_DESC render_target_desc;
        render_target->GetDesc(&render_target_desc);
        render_target->Release();

        auto width = static_cast<float>(render_target_desc.Width);
        auto height = static_cast<float>(render_target_desc.Height);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *loading_screen_start_time);

        // Each of these is a full register (4 floats) as far as Direct3D is concerned
        const float c_resolution[4] = { width, height, 0.0F, 0.0F };
        const float c_duration[4] = { static_cast<float>(SHADER_EFFECT_DURATION.count()) / 1000.0F, 0.0F, 0.0F, 0.0F };
        const float c_elapsed[4] = { static_cast<float>(elapsed.count()) / 1000.0F, 0.0F, 0.0F, 0.0F };
        const float c_opacity[4] = { static_cast<float>(loading_screen_alpha) / 255.0F, 0.0F, 0.0F, 0.0F };

        const DWORD white = D3DCOLOR_ARGB(255, 255, 255, 255);
        const Vertex vertices[4] = {
            { 0.0F, 0.0F, 0.0F, 1.0F, white, 0.0F, 0.0F },
            { width, 0.0F, 0.0F, 1.0F, white, 1.0F, 0.0F },
            { 0.0F, height, 0.0F, 1.0F, white, 0.0F, 1.0F },
            { width, height, 0.0F, 1.0F, white, 1.0F, 1.0F }
        };

        {
            SavedDeviceState saved_state(device);

            for(const auto &state : RENDER_STATES) {
                device->SetRenderState(state.state, state.value);
            }
            for(const auto &state : TEXTURE_STAGE_STATES) {
                device->SetTextureStageState(state.stage, state.state, state.value);
            }
            for(const auto &state : SAMPLER_STATES) {
                device->SetSamplerState(0, state.state, state.value);
            }

            device->SetVertexShader(nullptr);
            device->SetPixelShader(background_shader);
            device->SetPixelShaderConstantF(0, c_resolution, 1);
            device->SetPixelShaderConstantF(1, c_duration, 1);
            device->SetPixelShaderConstantF(2, c_elapsed, 1);
            device->SetPixelShaderConstantF(3, c_opacity, 1);
            device->SetFVF(VERTEX_FVF);
            device->SetTexture(0, background_texture);
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
        }

        if(loading_screen_demo && elapsed > DEMO_DURATION) {
            end_loading_screen_background();
        }
    }

    /**
     * This is called every time Halo draws its loading screen. We use it to decide when the loading screen starts and
     * ends, and to draw our background before Halo draws whatever else it wants on top (e.g. the text).
     */
    static void loading_screen_hook() noexcept {
        std::uint32_t flag = *game_loading_screen_flag;
        bool in_loading_screen = flag != 0;

        if(!loading_screen_playback && in_loading_screen) {
            play_loading_screen_background();
        }
        else if(!loading_screen_demo && loading_screen_playback && (!in_loading_screen || (loading_screen_blocked && flag == LOADING_SCREEN_FLAG_MULTIPLAYER))) {
            end_loading_screen_background();
        }

        draw_loading_screen_background();
    }

    extern "C" void loading_screen_background_render_hook(std::uint32_t color_mask) noexcept {
        // color_mask is ARGB
        loading_screen_alpha = static_cast<std::uint8_t>(color_mask >> 24);
    }

    static void on_end_scene(LPDIRECT3DDEVICE9 dev) noexcept {
        // If we get a different device, then anything we made on the old one is no good anymore
        if(dev != device) {
            release_resources();
            device = dev;
        }

        create_resources();

        // While loading, we draw in the loading screen hook. Demos are drawn on top of whatever Halo is drawing.
        if(loading_screen_demo) {
            draw_loading_screen_background();
        }
    }

    static DWORD WINAPI load_map_thread(LPVOID map_name) noexcept {
        return load_map_worker_asm(static_cast<const char *>(map_name));
    }

    extern "C" std::uint32_t load_map_override(const char *map_name) noexcept {
        bool show_loading_screen = !loading_screen_blocked && resources_ready() && std::strcmp(map_name, UI_MAP_NAME) != 0;

        HANDLE thread = nullptr;
        if(show_loading_screen) {
            thread = CreateThread(nullptr, 0, load_map_thread, const_cast<char *>(map_name), 0, nullptr);
        }

        // Load it the way Halo normally would
        if(!thread) {
            end_loading_screen_background();
            return load_map_worker_asm(map_name);
        }

        play_loading_screen_background();

        // Keep drawing the loading screen until the map is done loading. Halo is not going to do this for us since we
        // are in the middle of a function that Halo called.
        bool quit_requested = false;
        WPARAM quit_code = 0;
        while(WaitForSingleObject(thread, 0) == WAIT_TIMEOUT) {
            MSG message;
            if(PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
                if(message.message == WM_QUIT) {
                    // Don't lose this. We'll pass it on once we're done so the game can exit.
                    quit_requested = true;
                    quit_code = message.wParam;
                }
                else {
                    // Forward window messages so Windows doesn't think the game is frozen
                    TranslateMessage(&message);
                    DispatchMessageA(&message);
                }
                continue;
            }

            bool drew_something = false;
            HRESULT status = device->TestCooperativeLevel();
            if(status == D3D_OK) {
                device->BeginScene();
                game_loading_screen_function();
                device->EndScene();

                // If neither we nor Halo drew anything, there's nothing new to show (and the back buffer could contain anything)
                if(loading_screen_playback || *game_loading_screen_flag != 0) {
                    device->Present(nullptr, nullptr, nullptr, nullptr);
                    drew_something = true;
                }
            }

            // Don't spin a core if we didn't get to wait for the display
            if(!drew_something) {
                Sleep(1);
            }
        }

        DWORD result = 0;
        GetExitCodeThread(thread, &result);
        CloseHandle(thread);

        if(quit_requested) {
            PostQuitMessage(static_cast<int>(quit_code));
        }

        return result;
    }

    void set_loading_screen_blocked(bool blocked) noexcept {
        loading_screen_blocked = blocked;
    }

    bool play_loading_screen_demo() noexcept {
        if(!loading_screen_enabled || !resources_ready()) {
            return false;
        }

        loading_screen_start_time = std::nullopt;
        loading_screen_alpha = 255;
        loading_screen_demo = true;
        loading_screen_playback = true;
        return true;
    }

    void set_up_loading_screen() noexcept {
        auto &chimera = get_chimera();
        if(!chimera.feature_present("client_threaded_loading_screen")) {
            return;
        }
        if(!chimera.get_ini()->get_value_bool("halo.loading_screen").value_or(true)) {
            return;
        }

        auto &behavior_flags_sig = chimera.get_signature("d3d9_device_behavior_flags_sig");
        auto &load_map_function_sig = chimera.get_signature("load_map_function_sig");
        auto &render_function_sig = chimera.get_signature("loading_screen_render_function_sig");
        auto &background_render_call_sig = chimera.get_signature("loading_screen_background_render_call_sig");

        // This is supposed to be a call to the function that draws Halo's loading screen background. We redirect it
        // rather than hooking it so that it can be skipped.
        auto *background_render_call = background_render_call_sig.data();
        if(*reinterpret_cast<const std::uint8_t *>(background_render_call) != 0xE8) {
            return;
        }

        // Halo's loading screen flag is the address that is loaded at the start of the function that draws the loading
        // screen. Use the original bytes since spam_to_join changes them.
        std::uint32_t flag_address;
        std::memcpy(&flag_address, render_function_sig.original_data() + 1, sizeof(flag_address));
        game_loading_screen_flag = reinterpret_cast<const volatile std::uint32_t *>(static_cast<std::uintptr_t>(flag_address));
        game_loading_screen_function = reinterpret_cast<void (*)()>(render_function_sig.data());

        // Direct3D needs to know that we're going to use the device from more than one thread. This is only needed
        // when the device is created, so it has to be done now.
        static Hook behavior_flags_hook;
        write_jmp_call(behavior_flags_sig.data() + 0x5, behavior_flags_hook, reinterpret_cast<const void *>(set_d3d9_device_multithreaded_flag), nullptr, false);

        // Load maps on a separate thread so the loading screen can be drawn while that is going on
        static Hook load_map_hook;
        write_function_override(load_map_function_sig.data(), load_map_hook, reinterpret_cast<const void *>(load_map_override_asm), &load_map_function_address);

        // Draw our background before Halo draws the rest of the loading screen. We hook the instruction after the first
        // one because the first one is also modified by the spam_to_join command.
        static Hook loading_screen_function_hook;
        write_jmp_call(render_function_sig.data() + 0x5, loading_screen_function_hook, reinterpret_cast<const void *>(loading_screen_hook));

        // Follow Halo's loading screen background instead of drawing it
        auto call_offset = reinterpret_cast<std::uintptr_t>(loading_screen_background_render_asm) - reinterpret_cast<std::uintptr_t>(background_render_call + 0x5);
        overwrite(background_render_call + 0x1, static_cast<std::uint32_t>(call_offset));

        add_d3d9_end_scene_event(on_end_scene);

        loading_screen_enabled = true;
    }
}
