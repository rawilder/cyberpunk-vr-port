#pragma once
#include <d3d12.h>

// Foveated rendering on top of the engine's own VRS.
//
// REDengine already runs D3D12 Variable Rate Shading with a content-adaptive R8_UINT
// shading-rate image (its PrepareShadingRateImage node, 16 px tiles on NVIDIA) and binds
// it with RSSetShadingRateImage on exactly the passes it considers VRS-safe. Rather than
// classify passes ourselves, this module substitutes a radial image of OURS for the
// engine's at that bind: full rate around the fovea, coarser toward the edges. Where the
// engine does not bind VRS, nothing changes. Issue #86.
//
// The fovea is the gaze point when the runtime provides one (XR_EXT_eye_gaze_interaction,
// located by OpenXRManager each frame in VIEW space) and the centre of the eye image when
// it does not, or while tracking is lost. The pattern is regenerated whenever the centre
// moves by half a tile or a setting changes, and uploaded on the first command list that
// binds it; eye images live in a ring so a rewrite never touches an image a frame in
// flight may still read.
namespace cvr::foveation {
struct Settings {
    int enabled{};        // xr_foveation
    float inner{0.35f};   // xr_foveation_inner: radius of the full-rate centre, in half-widths of the view
    float outer{0.65f};   // xr_foveation_outer: outside this radius the coarsest rate applies
    int outerRate{4};     // xr_foveation_rate: 2 -> 2x2, 4 -> 4x4 (needs AdditionalShadingRatesSupported)
    int gaze{1};          // xr_foveation_gaze: follow the eye-tracked gaze when the runtime provides it
};
void SetSettings(const Settings&);
Settings GetSettings();

// From OpenXRManager. The gaze direction is in VIEW space (-Z forward, +Y up); `tracked`
// is the runtime's ORIENTATION_TRACKED bit. Called once per XR frame.
void SetGaze(float x,float y,float z,bool tracked);
// The frustum each XR eye (0 = left, 1 = right) was rendered with, as submitted. Angles in radians.
void SetEyeFov(int xrEye,float angleLeft,float angleRight,float angleUp,float angleDown);

// Called from the RSSetShadingRateImage detour with the engine's image. Returns the
// image to bind instead (ours when active, otherwise `native`). Never throws.
ID3D12Resource* Substitute(ID3D12GraphicsCommandList* list,ID3D12Resource* native);
// One-line status for the overlay / log.
const char* Status();
}
