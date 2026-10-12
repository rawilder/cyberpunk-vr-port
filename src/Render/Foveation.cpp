#include "Render/Foveation.hpp"
#include "Render/CommandResources.hpp"
#include "Stereo/StereoInternal.hpp"
#include "Utils/StereoLog.hpp"
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

extern "C" int CyberpunkVR_IsVrcamViewActive();
extern "C" int CyberpunkVR_MainIsRightEye;

namespace cvr::foveation {
namespace {
using Microsoft::WRL::ComPtr;
using cvr::detail::g_game_device;

std::mutex g_mutex;
Settings g_settings;
uint64_t g_generation=1; // bumped by SetSettings; an eye image is current when its generation and centre match
char g_status[160]="off";

// Device capability, queried once from the game's device.
struct Caps {bool queried{},tier2{},additionalRates{};UINT tile{};};
Caps g_caps;

// Gaze state. One cyclopean direction serves both eyes; the per-eye frustum turns it into a
// position in that eye's image. Smoothed a little, and held for a moment when tracking drops
// (a blink) before easing back to the centre.
struct Gaze {
    float x{},y{},z{-1.f};        // smoothed direction, VIEW space
    bool everTracked{};           // the runtime has produced a tracked sample at least once
    bool tracked{};
    ULONGLONG lastTrackedMs{};
    uint64_t samples{};
};
Gaze g_gaze;
struct EyeFov {float tanL{-1.f},tanR{1.f},tanU{1.f},tanD{-1.f};bool known{};};
std::array<EyeFov,2> g_fov; // indexed by XR eye

struct Image {
    ComPtr<ID3D12Resource> texture,upload;
    uint64_t generation{};    // settings generation baked into `texture`, 0 = never uploaded
    float cx{},cy{};          // centre baked into `texture`, in tiles
    bool shadingState{};      // texture currently in SHADING_RATE_SOURCE (else COPY_DEST)
};
struct Eye {
    UINT width{},height{};
    std::array<Image,8> ring; // gaze updates rewrite often; a deep ring keeps in-flight frames' images intact
    size_t current{};
};
std::array<Eye,2> g_eyes;     // indexed by the ENGINE view: 0 = MAIN, 1 = VRCAM

void QueryCaps() {
    if(g_caps.queried)return;g_caps.queried=true;
    if(!g_game_device)return;
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o{};
    if(FAILED(g_game_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,&o,sizeof(o))))return;
    g_caps.tier2=o.VariableShadingRateTier>=D3D12_VARIABLE_SHADING_RATE_TIER_2;
    g_caps.additionalRates=o.AdditionalShadingRatesSupported!=0;
    g_caps.tile=o.ShadingRateImageTileSize;
    cvr::log("[foveation] VRS tier=%d tile=%u additionalRates=%d",int(o.VariableShadingRateTier),o.ShadingRateImageTileSize,int(o.AdditionalShadingRatesSupported));
}

bool AcceptableNative(const D3D12_RESOURCE_DESC& d) {
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Format==DXGI_FORMAT_R8_UINT &&
           d.MipLevels==1 && d.DepthOrArraySize==1 && d.SampleDesc.Count==1 &&
           d.Width>=1 && d.Width<=512 && d.Height>=1 && d.Height<=512;
}

// Where the fovea sits in this XR eye's image, as a fraction of width/height (0..1 inside the
// image). Falls back to the centre without a tracked gaze or a known frustum.
bool GazeActive() {
    if(!g_settings.gaze || !g_gaze.everTracked)return false;
    if(g_gaze.tracked)return true;
    return GetTickCount64()-g_gaze.lastTrackedMs<300; // hold through a blink
}
void Centre(int xrEye,float& u,float& v) {
    u=0.5f;v=0.5f;
    const auto& f=g_fov[size_t(xrEye)];
    if(!GazeActive() || !f.known)return;
    if(g_gaze.z>-0.05f)return;                 // looking sideways past the frustum: keep the centre
    const float tx=g_gaze.x/-g_gaze.z,ty=g_gaze.y/-g_gaze.z;
    u=(tx-f.tanL)/(f.tanR-f.tanL);
    v=(f.tanU-ty)/(f.tanU-f.tanD);             // image rows run top-down
    if(u<-0.25f)u=-0.25f;if(u>1.25f)u=1.25f;if(v<-0.25f)v=-0.25f;if(v>1.25f)v=1.25f;
}

// Radial pattern about (cx,cy) in tiles: 1x1 inside `inner`, 2x2 up to `outer`, the chosen rate
// beyond. Radii are in half-widths of the view so the same numbers mean the same thing at every
// resolution.
void Fill(uint8_t* bytes,UINT rowPitch,UINT w,UINT h,float cx,float cy,const Settings& s,bool additionalRates) {
    const uint8_t coarse=(s.outerRate>=4 && additionalRates)?uint8_t(D3D12_SHADING_RATE_4X4):uint8_t(D3D12_SHADING_RATE_2X2);
    const float half=0.5f*float(w>h?w:h);
    for(UINT y=0;y<h;++y) {
        uint8_t* row=bytes+size_t(y)*rowPitch;
        for(UINT x=0;x<w;++x) {
            const float dx=(float(x)+0.5f-cx)/half,dy=(float(y)+0.5f-cy)/half;
            const float r=std::sqrt(dx*dx+dy*dy);
            row[x]=r<s.inner?uint8_t(D3D12_SHADING_RATE_1X1):r<s.outer?uint8_t(D3D12_SHADING_RATE_2X2):coarse;
        }
    }
}

// Records the upload of the pattern into `image` on `list` and leaves the texture in
// SHADING_RATE_SOURCE. Returns false if anything could not be created.
bool Upload(ID3D12GraphicsCommandList* list,Image& image,UINT w,UINT h,float cx,float cy,const Settings& s) {
    if(!image.texture) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;
        d.Format=DXGI_FORMAT_R8_UINT;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        if(FAILED(g_game_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&image.texture))))return false;
        image.shadingState=false;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};
    {const auto d=image.texture->GetDesc();g_game_device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);}
    if(!image.upload) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        if(FAILED(g_game_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&image.upload))))return false;
    }
    void* mapped{};const D3D12_RANGE none{};
    if(FAILED(image.upload->Map(0,&none,&mapped)) || !mapped)return false;
    Fill(static_cast<uint8_t*>(mapped)+footprint.Offset,footprint.Footprint.RowPitch,w,h,cx,cy,s,g_caps.additionalRates);
    image.upload->Unmap(0,nullptr);
    if(!cvr::gpu::KeepCommandResources(list,{image.texture.Get(),image.upload.Get()}))return false;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=image.texture.Get();b.Transition.Subresource=0;
    if(image.shadingState){b.Transition.StateBefore=D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;list->ResourceBarrier(1,&b);}
    D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=image.upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=image.texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.SubresourceIndex=0;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;list->ResourceBarrier(1,&b);
    image.shadingState=true;image.cx=cx;image.cy=cy;
    return true;
}
} // namespace

void SetSettings(const Settings& s) {
    std::lock_guard lock(g_mutex);
    Settings v=s;
    if(v.inner<0.05f)v.inner=0.05f;if(v.inner>1.5f)v.inner=1.5f;
    if(v.outer<v.inner)v.outer=v.inner;if(v.outer>2.0f)v.outer=2.0f;
    v.outerRate=v.outerRate>=4?4:2;v.gaze=v.gaze?1:0;
    if(v.enabled!=g_settings.enabled || v.inner!=g_settings.inner || v.outer!=g_settings.outer || v.outerRate!=g_settings.outerRate || v.gaze!=g_settings.gaze) {
        g_settings=v;++g_generation;
        if(!v.enabled)std::snprintf(g_status,sizeof(g_status),"off");
    }
}
Settings GetSettings() {std::lock_guard lock(g_mutex);return g_settings;}
const char* Status() {return g_status;}

void SetGaze(float x,float y,float z,bool tracked) {
    std::lock_guard lock(g_mutex);
    ++g_gaze.samples;
    if(tracked) {
        const float len=std::sqrt(x*x+y*y+z*z);if(len<1e-4f)return;
        x/=len;y/=len;z/=len;
        // Light smoothing: the eye saccades faster than any filter, and the fovea region is wide
        // enough that a frame of lag is invisible; this only takes the jitter off the tile edge.
        const float k=g_gaze.everTracked?0.6f:1.f;
        g_gaze.x+=(x-g_gaze.x)*k;g_gaze.y+=(y-g_gaze.y)*k;g_gaze.z+=(z-g_gaze.z)*k;
        g_gaze.everTracked=true;g_gaze.tracked=true;g_gaze.lastTrackedMs=GetTickCount64();
    } else {
        g_gaze.tracked=false;
        if(GetTickCount64()-g_gaze.lastTrackedMs>=300){g_gaze.x+=(0-g_gaze.x)*0.15f;g_gaze.y+=(0-g_gaze.y)*0.15f;g_gaze.z+=(-1.f-g_gaze.z)*0.15f;}
    }
}
void SetEyeFov(int xrEye,float angleLeft,float angleRight,float angleUp,float angleDown) {
    if(xrEye<0 || xrEye>1)return;
    std::lock_guard lock(g_mutex);
    auto& f=g_fov[size_t(xrEye)];
    f.tanL=std::tan(angleLeft);f.tanR=std::tan(angleRight);f.tanU=std::tan(angleUp);f.tanD=std::tan(angleDown);
    f.known=f.tanR>f.tanL+1e-3f && f.tanU>f.tanD+1e-3f;
}

ID3D12Resource* Substitute(ID3D12GraphicsCommandList* list,ID3D12Resource* native) {
    if(!list || !native)return native;
    std::lock_guard lock(g_mutex);
    if(!g_settings.enabled)return native;
    QueryCaps();
    if(!g_caps.tier2){static bool once{};if(!once){once=true;cvr::log("[foveation] disabled: device has no VRS tier 2");std::snprintf(g_status,sizeof(g_status),"off: no VRS tier 2");}return native;}
    const auto desc=native->GetDesc();
    if(!AcceptableNative(desc)){static bool once{};if(!once){once=true;cvr::log("[foveation] ignoring unexpected shading-rate image: dim=%d fmt=%d %llux%u",int(desc.Dimension),int(desc.Format),desc.Width,desc.Height);}return native;}
    const bool vrcam=CyberpunkVR_IsVrcamViewActive()!=0;
    const int xrEye=vrcam?(CyberpunkVR_MainIsRightEye?0:1):(CyberpunkVR_MainIsRightEye?1:0);
    Eye& eye=g_eyes[vrcam?1:0];
    const UINT w=UINT(desc.Width),h=desc.Height;
    if(eye.width!=w || eye.height!=h){for(auto& i:eye.ring)i=Image{};eye.width=w;eye.height=h;eye.current=0;}
    float u,v;Centre(xrEye,u,v);
    // Quantise to half a tile: the eye jitters, the image should not be rebuilt for it.
    const float cx=std::round(u*float(w)*2.f)*0.5f,cy=std::round(v*float(h)*2.f)*0.5f;
    Image* current=&eye.ring[eye.current];
    if(current->texture && current->generation==g_generation && current->cx==cx && current->cy==cy)return current->texture.Get();
    // Settings or centre changed, or first use: write the pattern into the NEXT ring slot, so a
    // frame still reading the previous image is never overwritten underneath it.
    const size_t next=current->texture?(eye.current+1)%eye.ring.size():eye.current;
    Image& image=eye.ring[next];
    if(!Upload(list,image,w,h,cx,cy,g_settings)){static bool once{};if(!once){once=true;cvr::log("[foveation] upload failed; leaving native VRS");}return native;}
    image.generation=g_generation;eye.current=next;
    const bool gazeActive=GazeActive() && g_fov[size_t(xrEye)].known;
    {static bool once{};if(!once){once=true;
        cvr::log("[foveation] active: %ux%u tiles (tile %u px), 1x1 < %.2f, 2x2 < %.2f, %s beyond; view=%s xrEye=%d gaze=%s",
            w,h,g_caps.tile,g_settings.inner,g_settings.outer,(g_settings.outerRate>=4 && g_caps.additionalRates)?"4x4":"2x2",vrcam?"VRCAM":"MAIN",xrEye,gazeActive?"tracked":"centre");}}
    {static bool gazeLogged{};if(gazeActive && !gazeLogged){gazeLogged=true;cvr::log("[foveation] eye tracking active: first gaze-driven image at (%.1f,%.1f) of %ux%u",cx,cy,w,h);}}
    std::snprintf(g_status,sizeof(g_status),"%s | %s beyond %.2f, 2x2 beyond %.2f, %ux%u tiles",
        gazeActive?"eye-tracked":(g_settings.gaze?(g_gaze.samples?"fixed (no tracked gaze)":"fixed (no eye data)"):"fixed"),
        (g_settings.outerRate>=4 && g_caps.additionalRates)?"4x4":"2x2",g_settings.outer,g_settings.inner,w,h);
    return image.texture.Get();
}
}
