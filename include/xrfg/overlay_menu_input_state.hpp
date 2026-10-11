#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace xrfg::overlay_input_detail {
// Relative raw mouse counts are framebuffer pixels. Legacy client coordinates
// contribute only a delta: desktop origin and DPI virtualization cannot move
// the virtual pointer away from the position drawn in the headset.
struct VirtualCursor {
    float x{512}, y{384};
    unsigned width{1024}, height{768};
    bool legacy_seed{}, absolute_seed{};
    float last_client_x{}, last_client_y{}, last_absolute_x{}, last_absolute_y{};
    void resize(unsigned w,unsigned h) noexcept {
        w=std::max(1u,w);h=std::max(1u,h);
        x*=float(w)/float(width);y*=float(h)/float(height);
        width=w;height=h;clamp();
    }
    void clamp() noexcept {
        x=std::clamp(x,0.F,float(width-1));y=std::clamp(y,0.F,float(height-1));
    }
    void relative(float dx,float dy) noexcept {
        if(std::isfinite(dx)&&std::isfinite(dy)){x+=dx;y+=dy;clamp();}
    }
    void client(float cx,float cy,float client_width,float client_height) noexcept {
        if(legacy_seed&&client_width>0&&client_height>0)
            relative((cx-last_client_x)*width/client_width,(cy-last_client_y)*height/client_height);
        last_client_x=cx;last_client_y=cy;legacy_seed=true;
    }
    void absolute(float ax,float ay) noexcept {
        if(absolute_seed)relative((ax-last_absolute_x)*width/65535.F,(ay-last_absolute_y)*height/65535.F);
        last_absolute_x=ax;last_absolute_y=ay;absolute_seed=true;
    }
    void reset_seeds() noexcept {legacy_seed=absolute_seed=false;}
};
struct ButtonMask {
    std::array<bool,256> down{}, blocked{};
    // A repeat of a key already held by the game must not turn its eventual
    // release into a blocked release. This is the accepted OptiInput rule.
    static bool transition(bool& down,bool& blocked,bool pressed,bool menu_open) noexcept {
        const bool was_down=down;
        if(pressed&&!was_down)blocked=menu_open;
        const bool suppress=menu_open ? (pressed||blocked) : blocked;
        down=pressed;
        if(!pressed)blocked=false;
        return suppress;
    }
    bool event(unsigned key,bool pressed,bool menu_open) noexcept {
        if(key>=down.size())return false;
        return transition(down[key],blocked[key],pressed,menu_open);
    }
    void clear() noexcept {down.fill(false);blocked.fill(false);}
};
struct BufferedButtons {
    std::unordered_map<std::uint32_t,std::array<bool,2>> offsets;
    bool suppress(std::uint32_t offset,bool pressed,bool open) {
        if(!offsets.contains(offset)&&offsets.size()>=512)return open&&pressed;
        auto& button=offsets[offset];
        return ButtonMask::transition(button[0],button[1],pressed,open);
    }
};
// Production buffered Raw Input reducer, shared by the Win32 API detour and
// portable packet fixtures. Every packet uses the same immutable masking
// decision and ordered framebuffer event path as WM_INPUT delivery.
void reduce_raw_buffer(void* data,std::size_t byte_capacity,unsigned packet_count);
}
