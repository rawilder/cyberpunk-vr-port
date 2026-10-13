#include "Overlay/HandWheel.hpp"
#include "Runtimes/OpenXRManager.hpp"
#include "Utils/StereoLog.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")

extern "C" __declspec(dllexport) int32_t CyberpunkVR_HandWheel = 1;          // 0 disables the wheel entirely
extern "C" __declspec(dllexport) int32_t CyberpunkVR_HandWheelHoldMs = 180;  // Y hold before the wheel opens
extern "C" __declspec(dllexport) float   CyberpunkVR_HandWheelDeadZone = 0.03f; // metres of hand travel before a sector lights
extern "C" __declspec(dllexport) float   CyberpunkVR_HandWheelSize = 0.16f;     // quad edge, metres
extern "C" __declspec(dllexport) int32_t CyberpunkVR_HandWheelMode = 1;         // 1 = the game's wheel on the hand, 0 = drawn wheel

namespace cvr::handwheel {
namespace {
constexpr uint32_t kCanvas=512;
constexpr float kPi=3.14159265358979f;

struct V3 {float x,y,z;};
V3 Sub(const XrVector3f& a,const XrVector3f& b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float Dot(const V3& a,const V3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V3 Cross(const V3& a,const V3& b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V3 Norm(const V3& a){const float l=std::sqrt(Dot(a,a));return l>1e-6f?V3{a.x/l,a.y/l,a.z/l}:V3{0,0,1};}
V3 Scale(const V3& a,float s){return {a.x*s,a.y*s,a.z*s};}
// Quaternion whose columns are the basis (x=r, y=u, z=f); right-handed, z toward the viewer.
XrQuaternionf FromBasis(const V3& r,const V3& u,const V3& f) {
    const float t=r.x+u.y+f.z;XrQuaternionf q{};
    if(t>0){const float s=std::sqrt(t+1.f)*2.f;q.w=.25f*s;q.x=(u.z-f.y)/s;q.y=(f.x-r.z)/s;q.z=(r.y-u.x)/s;}
    else if(r.x>u.y && r.x>f.z){const float s=std::sqrt(1.f+r.x-u.y-f.z)*2.f;q.w=(u.z-f.y)/s;q.x=.25f*s;q.y=(u.x+r.y)/s;q.z=(f.x+r.z)/s;}
    else if(u.y>f.z){const float s=std::sqrt(1.f+u.y-r.x-f.z)*2.f;q.w=(f.x-r.z)/s;q.x=(u.x+r.y)/s;q.y=.25f*s;q.z=(f.y+u.z)/s;}
    else{const float s=std::sqrt(1.f+f.z-r.x-u.y)*2.f;q.w=(r.y-u.x)/s;q.x=(f.x+r.z)/s;q.y=(f.y+u.z)/s;q.z=.25f*s;}
    return q;
}

struct State {
    std::mutex mutex;
    std::vector<std::string> labels{"1","2","3","Holster"};
    std::deque<int> commands;
    cvr::vrui::Tracking tracking{};
    bool open{},yWas{},bWas{};
    unsigned long long yDownMs{},pulseUntilMs{};
    XrVector3f anchor{};V3 r{1,0,0},u{0,1,0},f{0,0,1};
    bool anchored{},nativeVisible{};   // native mode: Y held long enough to place the quad / the quad was shown
    float stickX{},stickY{};           // native mode: synthesized right stick from hand displacement
    int selection=-1;
    uint64_t generation=1;   // bumped whenever the drawn content changes
    XrPosef pose{{0,0,0,1},{}};
};
State g;

void Push(int c){g.commands.push_back(c);if(g.commands.size()>16)g.commands.pop_front();}

// The ring basis, from the hand toward the head, with world up.
void Rebase() {
    const V3 toHead=Norm(Sub(g.tracking.head.position,g.anchor));
    V3 up{0,1,0};up=Norm(V3{up.x-toHead.x*Dot(up,toHead),up.y-toHead.y*Dot(up,toHead),up.z-toHead.z*Dot(up,toHead)});
    g.f=toHead;g.u=up;g.r=Norm(Cross(g.u,g.f));
    g.pose.orientation=FromBasis(g.r,g.u,g.f);
    const V3 p=V3{g.anchor.x,g.anchor.y,g.anchor.z};const V3 lift=Scale(g.f,.02f);
    g.pose.position={p.x+lift.x,p.y+lift.y,p.z+lift.z};
}

// ---- drawing ----------------------------------------------------------------------------
// Pixels are R8G8B8A8 in memory order (0xAABBGGRR as a little-endian uint32). Straight alpha,
// composited by the runtime with BLEND_TEXTURE_SOURCE_ALPHA.
struct RGBA {float r,g,b,a;};
constexpr RGBA kRing{.10f,.09f,.08f,.78f};        // the dark, slightly warm disc
constexpr RGBA kRingLit{.95f,.90f,.00f,.96f};      // the game's yellow
constexpr RGBA kSeparator{.55f,.52f,.45f,.55f};
constexpr RGBA kHub{.08f,.07f,.06f,.85f};
constexpr RGBA kHubRim{.95f,.90f,.00f,.90f};
constexpr RGBA kText{.96f,.96f,.94f,1.f};
constexpr RGBA kTextLit{.08f,.07f,.05f,1.f};

uint32_t Pack(const RGBA& c) {
    auto q=[](float v){return uint32_t(std::clamp(v,0.f,1.f)*255.f+.5f);};
    return q(c.r)|(q(c.g)<<8)|(q(c.b)<<16)|(q(c.a)<<24);
}
void Over(RGBA& dst,const RGBA& src,float cover) {
    const float a=src.a*cover;if(a<=0)return;
    dst.r=src.r*a+dst.r*(1-a);dst.g=src.g*a+dst.g*(1-a);dst.b=src.b*a+dst.b*(1-a);dst.a=a+dst.a*(1-a);
}

// Which sector a canvas point belongs to, and how far into it. Sector 0 is centred on top,
// the rest run clockwise.
struct Sector {int index;float local;};
Sector SectorAt(float dx,float dy,int n) {
    const float sector=2.f*kPi/float(n);
    float from=kPi*.5f-std::atan2(dy,dx);while(from<0)from+=2*kPi;while(from>=2*kPi)from-=2*kPi;
    const float t=from+sector*.5f;const int i=int(t/sector)%n;
    return {i,t-float(i)*sector};
}

// Text through GDI: a real anti-aliased font, drawn white on black into a DIB, then used as a
// coverage mask. The game process already has user32/gdi32 loaded for the launcher window.
struct TextMask {
    std::vector<uint8_t> cover;int w{},h{};
    bool Render(const wchar_t* text,int px,bool bold) {
        HDC screen=GetDC(nullptr);if(!screen)return false;
        HDC dc=CreateCompatibleDC(screen);ReleaseDC(nullptr,screen);if(!dc)return false;
        HFONT font=CreateFontW(-px,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        if(!font){DeleteDC(dc);return false;}
        auto* old=SelectObject(dc,font);
        SIZE size{};GetTextExtentPoint32W(dc,text,int(wcslen(text)),&size);
        w=size.cx+4;h=size.cy+2;
        BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(bmi.bmiHeader);bmi.bmiHeader.biWidth=w;bmi.bmiHeader.biHeight=-h;bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
        void* bits{};HBITMAP bmp=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bmp || !bits){SelectObject(dc,old);DeleteObject(font);DeleteDC(dc);return false;}
        auto* oldBmp=SelectObject(dc,bmp);
        std::memset(bits,0,size_t(w)*h*4);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        TextOutW(dc,2,1,text,int(wcslen(text)));
        GdiFlush();
        cover.resize(size_t(w)*h);
        const auto* px32=static_cast<const uint32_t*>(bits);
        for(size_t i=0;i<cover.size();++i)cover[i]=uint8_t((px32[i]>>8)&0xFF); // green channel = coverage
        SelectObject(dc,oldBmp);SelectObject(dc,old);DeleteObject(bmp);DeleteObject(font);DeleteDC(dc);
        return true;
    }
};

void Draw(std::vector<RGBA>& img,int W,int H,const std::vector<std::string>& labels,int selection) {
    std::fill(img.begin(),img.end(),RGBA{0,0,0,0});
    const int n=int(labels.size());if(n<1)return;
    const float cx=W*.5f,cy=H*.5f,half=std::min(cx,cy);
    const float rIn=half*.36f,rOut=half*.96f,hub=half*.13f;
    const float sector=2.f*kPi/float(n),gap=.028f;            // radians of gap at each separator
    constexpr int SS=3;                                        // supersampling per axis
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        float ring=0,lit=0,sep=0,hubC=0,rim=0;
        for(int sy=0;sy<SS;++sy)for(int sx=0;sx<SS;++sx) {
            const float px=float(x)+(sx+.5f)/SS,py=float(y)+(sy+.5f)/SS;
            const float dx=px-cx,dy=cy-py;const float d=std::sqrt(dx*dx+dy*dy);
            if(d<hub){hubC+=1;if(d>hub-2.5f)rim+=1;continue;}
            const auto s=SectorAt(dx,dy,n);
            const bool selected=s.index==selection;
            const float out=selected?rOut:rOut-half*.03f;      // the lit sector stands a little proud
            if(d<rIn || d>out)continue;
            if(s.local<gap || s.local>sector-gap){sep+=1;continue;}
            if(selected)lit+=1;else ring+=1;
        }
        const float k=1.f/(SS*SS);RGBA& o=img[size_t(y)*W+x];
        Over(o,kRing,ring*k);Over(o,kRingLit,lit*k);Over(o,kSeparator,sep*k*.6f);Over(o,kHub,hubC*k);Over(o,kHubRim,rim*k);
    }
    // Labels, centred on the ring's mid radius.
    for(int i=0;i<n;++i) {
        const float a=kPi*.5f-float(i)*sector;const float rad=(rIn+rOut)*.5f;
        std::wstring text;for(unsigned char ch:labels[size_t(i)])text+=wchar_t(ch);
        if(text.size()>14){text.resize(13);text+=L"…";}
        TextMask mask;const int px=text.size()>9?int(half*.105f):int(half*.125f);
        if(!mask.Render(text.c_str(),px,true))continue;
        const int ox=int(cx+std::cos(a)*rad)-mask.w/2,oy=int(cy-std::sin(a)*rad)-mask.h/2;
        const RGBA& color=i==selection?kTextLit:kText;
        for(int y=0;y<mask.h;++y)for(int x=0;x<mask.w;++x) {
            const int X=ox+x,Y=oy+y;if(X<0 || Y<0 || X>=W || Y>=H)continue;
            const float c=mask.cover[size_t(y)*mask.w+x]/255.f;if(c<=0)continue;
            Over(img[size_t(Y)*W+X],color,c);
        }
    }
}
} // namespace

bool DrawnMode(){return CyberpunkVR_HandWheelMode==0;}
bool NativeAnchor(XrPosef& out){std::lock_guard lock(g.mutex);if(DrawnMode() || !g.anchored)return false;out=g.pose;return true;}
bool NativeStick(float& x,float& y){std::lock_guard lock(g.mutex);if(DrawnMode() || !g.anchored || !g.nativeVisible)return false;x=g.stickX;y=g.stickY;return true;}
void SetNativeVisible(bool v){std::lock_guard lock(g.mutex);g.nativeVisible=v;}

void SetLabels(const char* s) {
    std::lock_guard lock(g.mutex);
    std::vector<std::string> out;std::string cur;
    for(const char* p=s?s:"";;++p){if(*p=='|' || !*p){if(!cur.empty() || *p=='|')out.push_back(cur);cur.clear();if(!*p)break;}else cur+=*p;}
    if(out.empty())out={"1","2","3","Holster"};if(out.size()>8)out.resize(8);
    if(out!=g.labels){g.labels=out;++g.generation;}
}
int PollCommand(){std::lock_guard lock(g.mutex);if(g.commands.empty())return 0;const int c=g.commands.front();g.commands.pop_front();return c;}
bool Open(){std::lock_guard lock(g.mutex);return g.open;}
bool WantsTracking(){std::lock_guard lock(g.mutex);return CyberpunkVR_HandWheel!=0 && (g.open || g.yWas);}
bool PulseY(){std::lock_guard lock(g.mutex);return GetTickCount64()<g.pulseUntilMs;}

void UpdateTracking(const cvr::vrui::Tracking& t) {
    std::lock_guard lock(g.mutex);
    g.tracking=t;
    if(!g.open && !g.anchored)return;
    const auto& hand=t.hands[1];if(!hand.valid)return;
    const V3 d=Sub(hand.aim.position,g.anchor);
    const float dx=Dot(d,g.r),dy=Dot(d,g.u);const float dist=std::sqrt(dx*dx+dy*dy);
    if(g.anchored) {
        // Native mode: full deflection toward the hand once past the dead zone, so the game's
        // own wheel highlights the sector the hand went to; centred = nothing selected.
        if(dist>=CyberpunkVR_HandWheelDeadZone){g.stickX=dx/dist;g.stickY=dy/dist;}else{g.stickX=0;g.stickY=0;}
        return;
    }
    int sel=-1;
    if(dist>=CyberpunkVR_HandWheelDeadZone)sel=SectorAt(dx,dy,int(g.labels.size())).index;
    if(sel!=g.selection){g.selection=sel;++g.generation;}
}

bool UpdateInput(const VRControllerState& vr,unsigned long long now) {
    std::lock_guard lock(g.mutex);
    if(CyberpunkVR_HandWheel==0){g.open=false;g.yWas=false;return false;}
    const bool y=(vr.buttons&0x8000)!=0,b=(vr.buttons&0x2000)!=0;
    if(y && !g.yWas)g.yDownMs=now;
    const unsigned long long hold=CyberpunkVR_HandWheelHoldMs>0?unsigned long long(CyberpunkVR_HandWheelHoldMs):180ull;
    if(!DrawnMode()) {
        // Native mode: Y goes to the game untouched. We only place the quad once the hold is
        // long enough that the game's wheel is coming, and synthesize the stick from there.
        if(y && !g.anchored && now-g.yDownMs>=hold && g.tracking.valid && g.tracking.hands[1].valid) {
            g.anchored=true;g.anchor=g.tracking.hands[1].aim.position;g.stickX=g.stickY=0;Rebase();
            cvr::log("[handwheel] native anchor");
        }
        if(!y && g.anchored){g.anchored=false;g.nativeVisible=false;g.stickX=g.stickY=0;}
        g.yWas=y;g.bWas=b;g.open=false;
        return false;
    }
    if(y && !g.open && now-g.yDownMs>=hold && g.tracking.valid && g.tracking.hands[1].valid) {
        g.open=true;g.anchor=g.tracking.hands[1].aim.position;g.selection=-1;Rebase();++g.generation;Push(1);
        cvr::log("[handwheel] open");
    }
    if(!y && g.yWas) {
        if(g.open){if(g.selection>=0)Push(100+g.selection);Push(2);g.open=false;cvr::log("[handwheel] release -> %d",g.selection);}
        else if(now-g.yDownMs<hold)g.pulseUntilMs=now+70;   // a tap: hand it to the game as a tap
    }
    if(g.open && b && !g.bWas){g.selection=-1;g.open=false;Push(2);cvr::log("[handwheel] cancelled");}
    g.yWas=y;g.bWas=b;
    return y || g.open;
}

const XrCompositionLayerBaseHeader* Wheel::Prepare(XrSession session,XrSpace local,ID3D12Device* device,ID3D12CommandQueue* queue) {
    bool open;uint64_t generation;XrPosef pose;std::vector<std::string> labels;int selection;
    {std::lock_guard lock(g.mutex);open=g.open;generation=g.generation;pose=g.pose;labels=g.labels;selection=g.selection;}
    if(!open){published=false;return nullptr;}
    if(!device || !queue || !session || !local)return nullptr;
    if(!swapchain) {
        width=height=kCanvas;rowPitch=(width*4+255)&~255u;pixels.resize(size_t(width)*height);
        uint32_t count{};if(XR_FAILED(xrEnumerateSwapchainFormats(session,0,&count,nullptr)))return nullptr;
        std::vector<int64_t> formats(count);if(XR_FAILED(xrEnumerateSwapchainFormats(session,count,&count,formats.data())))return nullptr;
        if(std::find(formats.begin(),formats.end(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)==formats.end())return nullptr;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.sampleCount=1;info.width=width;info.height=height;info.faceCount=1;info.arraySize=1;info.mipCount=1;
        if(XR_FAILED(xrCreateSwapchain(session,&info,&swapchain)))return nullptr;
        if(XR_FAILED(xrEnumerateSwapchainImages(swapchain,0,&count,nullptr))){Shutdown();return nullptr;}
        images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(XR_FAILED(xrEnumerateSwapchainImages(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))) ||
           FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))) ||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))) ||
           FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))){Shutdown();return nullptr;}
        list->Close();D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=UINT64(rowPitch)*height;
        desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)))){Shutdown();return nullptr;}
        drawnGeneration=0;
    }
    if((drawnGeneration!=generation || acquired) && fence->GetCompletedValue()>=fenceValue) {
        if(!acquired){XrSwapchainImageAcquireInfo info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(XR_SUCCEEDED(xrAcquireSwapchainImage(swapchain,&info,&acquiredIndex)))acquired=true;}
        if(acquired) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
            if(xrWaitSwapchainImage(swapchain,&wait)==XR_SUCCESS) {
                void* data{};D3D12_RANGE none{};
                if(SUCCEEDED(upload->Map(0,&none,&data))) {
                    static std::vector<RGBA> img;img.resize(size_t(width)*height);
                    Draw(img,int(width),int(height),labels,selection);
                    for(size_t i=0;i<img.size();++i)pixels[i]=Pack(img[i]);
                    for(uint32_t row=0;row<height;++row)std::memcpy(static_cast<uint8_t*>(data)+size_t(row)*rowPitch,pixels.data()+size_t(row)*width,size_t(width)*4);
                    upload->Unmap(0,nullptr);
                    if(SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.Get(),nullptr))) {
                        auto* target=images[acquiredIndex].texture;
                        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        b.Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&b);
                        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                        src.PlacedFootprint.Footprint={target->GetDesc().Format,width,height,1,rowPitch};dst.pResource=target;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
                        if(SUCCEEDED(list->Close())){ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);queue->Signal(fence.Get(),++fenceValue);published=true;drawnGeneration=generation;}
                    }
                }
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if(XR_FAILED(xrReleaseSwapchainImage(swapchain,&release)))published=false;
                acquired=false;
            }
        }
    }
    if(!published)return nullptr;
    layer.space=local;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    layer.subImage={swapchain,{{0,0},{int32_t(width),int32_t(height)}},0};
    layer.pose=pose;const float s=CyberpunkVR_HandWheelSize>.05f?CyberpunkVR_HandWheelSize:.16f;layer.size={s,s};
    return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
}
void Wheel::Shutdown(bool wait) {
    if(!swapchain && !fence && pixels.empty())return;
    if(wait && fence && fence->GetCompletedValue()<fenceValue) {
        const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(event){if(SUCCEEDED(fence->SetEventOnCompletion(fenceValue,event)))WaitForSingleObject(event,1000);CloseHandle(event);}
    }
    if(fence && fence->GetCompletedValue()<fenceValue)return;
    if(acquired && swapchain){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xrReleaseSwapchainImage(swapchain,&release);acquired=false;}
    if(swapchain){xrDestroySwapchain(swapchain);swapchain=XR_NULL_HANDLE;}
    images.clear();allocator.Reset();list.Reset();upload.Reset();fence.Reset();pixels.clear();published=false;fenceValue=0;
}
}
