#pragma once
#include <windows.h>
#include <d3d12.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
#include <vector>
#include "Overlay/VrInteraction.hpp"

struct VRControllerState;

// Hand-anchored weapon wheel (Alyx-style). Hold the right Y: a ring of slots appears on the
// right hand, facing the head, and the game is slowed (CET side). Move the hand toward a slot
// to light it; release Y to take it, B to cancel. The game's own full-screen wheel never
// opens: the Y hold is masked in the XInput merge before the game sees it, and a short tap
// is replayed afterwards so tap-to-cycle still works.
//
// Drawn as its own OpenXR quad layer with a CPU canvas (the framegen stats overlay is the
// template), so no game asset is involved. Labels come from CET through VRHandWheelLabels;
// choices go back through VRHandWheelCommand (0 nothing, 1 opened, 2 closed, 100+i chosen).
extern "C" __declspec(dllexport) extern float CyberpunkVR_HandWheelSize;

namespace cvr::handwheel {
// Mode 1 (default): the GAME's radial wheel is re-hosted on the hand (HUD channel Wheel) and
// steered by hand displacement through a synthesized right stick; Y reaches the game.
// Mode 0: the module draws its own wheel (below) and equips through CET.
bool DrawnMode();
bool NativeAnchor(XrPosef& out);           // where the native wheel quad sits, while Y is held
bool NativeStick(float& x,float& y);       // synthesized right stick while the native wheel is up
void SetNativeVisible(bool);               // the frame loop reports whether the wheel quad was shown
void SetLabels(const char* pipeSeparated);
int PollCommand();
bool Open();
bool WantsTracking();                       // Y held or wheel open: the frame loop locates the aim pose
void UpdateTracking(const cvr::vrui::Tracking&);
// From the XInput hook, every poll. Returns true while the wheel owns the right Y and B.
bool UpdateInput(const VRControllerState& vr,unsigned long long nowMs);
bool PulseY();                              // replay a tap as a press for a few ms after release

class Wheel {
public:
    const XrCompositionLayerBaseHeader* Prepare(XrSession,XrSpace local,ID3D12Device*,ID3D12CommandQueue*);
    void Shutdown(bool wait=false);
private:
    XrSwapchain swapchain{};
    std::vector<XrSwapchainImageD3D12KHR> images;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    uint64_t fenceValue{};
    uint32_t acquiredIndex{};bool acquired{},published{};
    uint32_t width{},height{},rowPitch{};
    std::vector<uint32_t> pixels;
    uint64_t drawnGeneration{};
};
}
