#include "Overlay/HandWheel.hpp"
#include "Runtimes/OpenXRManager.hpp"
#include "Utils/StereoLog.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

extern "C" __declspec(dllexport) int32_t CyberpunkVR_HandWheel = 1;          // 0 disables the wheel entirely
extern "C" __declspec(dllexport) int32_t CyberpunkVR_HandWheelHoldMs = 180;  // Y hold before the wheel opens
extern "C" __declspec(dllexport) float   CyberpunkVR_HandWheelDeadZone = 0.03f; // metres of hand travel before a sector lights
extern "C" __declspec(dllexport) float   CyberpunkVR_HandWheelSize = 0.16f;     // quad edge, metres

namespace cvr::handwheel {
namespace {
constexpr uint32_t kCanvas=256;
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
    std::vector<std::string> labels{"1","2","3","HOLSTER"};
    std::deque<int> commands;
    cvr::vrui::Tracking tracking{};
    bool open{},yWas{},bWas{};
    unsigned long long yDownMs{},pulseUntilMs{};
    XrVector3f anchor{};V3 r{1,0,0},u{0,1,0},f{0,0,1};
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

// 5x7 glyphs, the same bounded alphabet the stats overlay draws with.
const uint8_t* Glyph(char c) {
    static constexpr uint8_t digits[][5]={{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
        {39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30}};
    static constexpr uint8_t letters[][5]={{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
        {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
        {127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
        {62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},
        {63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67}};
    static constexpr uint8_t blank[]={0,0,0,0,0},dash[]={8,8,8,8,8};
    if(c>='0' && c<='9')return digits[c-'0'];
    if(c>='A' && c<='Z')return letters[c-'A'];
    if(c>='a' && c<='z')return letters[c-'a'];
    return c=='-' ? dash : blank;
}
struct Canvas {
    uint32_t* px;int w,h;
    void Text(int x,int y,const char* s,uint32_t color,int scale) {
        for(const char* p=s;*p;++p,x+=6*scale){const auto* g2=Glyph(*p);
            for(int col=0;col<5;++col)for(int row=0;row<7;++row)if(g2[col]&(1u<<row))
                for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx){const int X=x+col*scale+dx,Y=y+row*scale+dy;
                    if(X>=0 && X<w && Y>=0 && Y<h)px[Y*w+X]=color;}}
    }
};

// Colours are ABGR in memory order for an R8G8B8A8 swapchain.
constexpr uint32_t kSector=0xB8302A24u,kSectorLit=0xF000D8F2u,kEdge=0xFF8C8C8Cu,kText=0xFFF2F2F2u,kTextLit=0xFF101010u,kHub=0xC0181412u;

void Draw(Canvas c,const std::vector<std::string>& labels,int selection) {
    std::fill(c.px,c.px+size_t(c.w)*c.h,0u);
    const int n=int(labels.size());if(n<1)return;
    const float cx=c.w*.5f,cy=c.h*.5f,half=std::min(cx,cy);
    const float rIn=half*.34f,rOut=half*.98f,hub=half*.14f;
    const float sector=2.f*kPi/float(n),gap=.035f;
    for(int y=0;y<c.h;++y)for(int x=0;x<c.w;++x) {
        const float dx=float(x)+.5f-cx,dy=cy-(float(y)+.5f);const float d=std::sqrt(dx*dx+dy*dy);
        if(d<hub){c.px[y*c.w+x]=kHub;continue;}
        if(d<rIn || d>rOut)continue;
        // Sector 0 is centred on top, the rest run clockwise.
        float a=std::atan2(dy,dx);float from=kPi*.5f-a;while(from<0)from+=2*kPi;while(from>=2*kPi)from-=2*kPi;
        const float t=from+sector*.5f;const int i=int(t/sector)%n;const float local=t-float(i)*sector;
        if(local<gap || local>sector-gap)continue;
        const bool edge=d<rIn+2.f || d>rOut-2.f;
        c.px[y*c.w+x]=edge?kEdge:(i==selection?kSectorLit:kSector);
    }
    for(int i=0;i<n;++i) {
        const float a=kPi*.5f-float(i)*sector;const float rad=(rIn+rOut)*.5f;
        std::string s=labels[size_t(i)];for(auto& ch:s)ch=char(toupper(unsigned char(ch)));if(s.size()>10)s.resize(10);
        const int scale=s.size()>7?1:2;const int wpx=int(s.size())*6*scale-scale;
        c.Text(int(cx+std::cos(a)*rad)-wpx/2,int(cy-std::sin(a)*rad)-7*scale/2,s.c_str(),i==selection?kTextLit:kText,scale);
    }
}
} // namespace

void SetLabels(const char* s) {
    std::lock_guard lock(g.mutex);
    std::vector<std::string> out;std::string cur;
    for(const char* p=s?s:"";;++p){if(*p=='|' || !*p){if(!cur.empty() || *p=='|')out.push_back(cur);cur.clear();if(!*p)break;}else cur+=*p;}
    if(out.empty())out={"1","2","3","HOLSTER"};if(out.size()>8)out.resize(8);
    if(out!=g.labels){g.labels=out;++g.generation;}
}
int PollCommand(){std::lock_guard lock(g.mutex);if(g.commands.empty())return 0;const int c=g.commands.front();g.commands.pop_front();return c;}
bool Open(){std::lock_guard lock(g.mutex);return g.open;}
bool WantsTracking(){std::lock_guard lock(g.mutex);return CyberpunkVR_HandWheel!=0 && (g.open || g.yWas);}
bool PulseY(){std::lock_guard lock(g.mutex);return GetTickCount64()<g.pulseUntilMs;}

void UpdateTracking(const cvr::vrui::Tracking& t) {
    std::lock_guard lock(g.mutex);
    g.tracking=t;
    if(!g.open)return;
    const auto& hand=t.hands[1];if(!hand.valid)return;
    const V3 d=Sub(hand.aim.position,g.anchor);
    const float dx=Dot(d,g.r),dy=Dot(d,g.u);const float dist=std::sqrt(dx*dx+dy*dy);
    int sel=-1;
    if(dist>=CyberpunkVR_HandWheelDeadZone) {
        const int n=int(g.labels.size());const float sector=2.f*kPi/float(n);
        float from=kPi*.5f-std::atan2(dy,dx);while(from<0)from+=2*kPi;
        sel=int((from+sector*.5f)/sector)%n;
    }
    if(sel!=g.selection){g.selection=sel;++g.generation;}
}

bool UpdateInput(const VRControllerState& vr,unsigned long long now) {
    std::lock_guard lock(g.mutex);
    if(CyberpunkVR_HandWheel==0){g.open=false;g.yWas=false;return false;}
    const bool y=(vr.buttons&0x8000)!=0,b=(vr.buttons&0x2000)!=0;
    if(y && !g.yWas)g.yDownMs=now;
    const unsigned long long hold=CyberpunkVR_HandWheelHoldMs>0?unsigned long long(CyberpunkVR_HandWheelHoldMs):180ull;
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
                    Draw({pixels.data(),int(width),int(height)},labels,selection);
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
