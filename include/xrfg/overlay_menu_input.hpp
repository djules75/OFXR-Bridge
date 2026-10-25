#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace xrfg {
enum class OverlayMenuInputEventKind {mouse_position,mouse_button,wheel,key,text,focus};
struct OverlayMenuInputEvent {
    OverlayMenuInputEventKind kind{};
    float x{},y{};
    unsigned code{};
    bool down{};
};
struct OverlayMenuInputFrame {
    bool open{},focused{},mouse_valid{};
    // HWND-thread acquisition may still be pending after F10 on the render
    // thread. A closed frame with requested_open=true must not cancel it.
    bool requested_open{},transitioning{};
    float mouse_x{512},mouse_y{384},wheel_x{},wheel_y{};
    std::array<bool,5> mouse_down{};
    std::array<bool,256> keys{};
    std::vector<OverlayMenuInputEvent> events;
};
// Process input follows only the actual foreground HWND owned by this process.
// Relative input drives a virtual cursor in this framebuffer's coordinates;
// no projected headset rectangle is mapped onto desktop pixels.
class OverlayMenuInput {
public:
    OverlayMenuInput();
    ~OverlayMenuInput();
    OverlayMenuInput(const OverlayMenuInput&)=delete;
    OverlayMenuInput& operator=(const OverlayMenuInput&)=delete;
    OverlayMenuInputFrame poll(unsigned framebuffer_width=1024,unsigned framebuffer_height=768);
    bool set_open(bool open);
    void close() noexcept;
    // Countershift a consumed menu drag in the same framebuffer coordinates.
    // Only the current open owner may move its virtual cursor. Queued virtual
    // positions are rebased; native desktop state and raw/client origins stay
    // unchanged, so later input keeps its native delta.
    void offset_pointer(float delta_x,float delta_y) noexcept;
private:
    std::uint64_t owner_{};
};
// The renderer calls this only with its OWN ImGui context current, before
// NewFrame. Ordered button/key events preserve clicks within one render frame.
void apply_imgui_input(const OverlayMenuInputFrame& frame);
}
