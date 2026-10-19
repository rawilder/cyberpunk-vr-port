#include "Core/CpuAffinity.hpp"
#include "Core/VrCoreShared.hpp"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cvr::cpu {
namespace {
int g_applied=-1;
char g_status[160]="not applied";
DWORD_PTR g_originalMask=0;DWORD g_originalPriority=0;bool g_originalSaved=false;

// P-core logical processors: every core whose EfficiencyClass is above the minimum seen.
bool PerformanceCoreMask(DWORD_PTR& out,int& pCores,int& eCores) {
    DWORD bytes=0;
    GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !bytes)return false;
    std::vector<uint8_t> buffer(bytes);
    if(!GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()),&bytes))return false;
    BYTE minClass=255,maxClass=0;
    for(DWORD off=0;off<bytes;){
        const auto* info=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()+off);
        if(info->Relationship==RelationProcessorCore){const BYTE c=info->Processor.EfficiencyClass;if(c<minClass)minClass=c;if(c>maxClass)maxClass=c;}
        off+=info->Size;
    }
    if(maxClass==minClass)return false; // homogeneous CPU: nothing to do
    out=0;pCores=eCores=0;
    for(DWORD off=0;off<bytes;){
        const auto* info=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()+off);
        if(info->Relationship==RelationProcessorCore){
            if(info->Processor.EfficiencyClass>minClass){++pCores;for(WORD g=0;g<info->Processor.GroupCount;++g)if(info->Processor.GroupMask[g].Group==0)out|=info->Processor.GroupMask[g].Mask;}
            else ++eCores;
        }
        off+=info->Size;
    }
    return out!=0;
}
}

void Apply(int mode) {
    if(mode==g_applied)return;
    const HANDLE process=GetCurrentProcess();
    if(!g_originalSaved){DWORD_PTR sys=0;if(GetProcessAffinityMask(process,&g_originalMask,&sys)){g_originalPriority=GetPriorityClass(process);g_originalSaved=true;}}
    if(mode<=0){
        if(g_applied>0 && g_originalSaved){SetProcessAffinityMask(process,g_originalMask);SetPriorityClass(process,g_originalPriority);Log("CpuAffinity: restored original affinity/priority\n");}
        std::snprintf(g_status,sizeof(g_status),"off");g_applied=mode;return;
    }
    DWORD_PTR mask=0;int p=0,e=0;
    if(!PerformanceCoreMask(mask,p,e)){std::snprintf(g_status,sizeof(g_status),"no efficiency classes on this CPU; nothing changed");Log("CpuAffinity: %s\n",g_status);g_applied=mode;return;}
    const bool okAffinity=SetProcessAffinityMask(process,mask)!=0;
    const DWORD priority=mode>=2?HIGH_PRIORITY_CLASS:ABOVE_NORMAL_PRIORITY_CLASS;
    const bool okPriority=SetPriorityClass(process,priority)!=0;
    std::snprintf(g_status,sizeof(g_status),"%d P-cores (mask 0x%llX), %d E-cores excluded, priority %s%s%s",p,static_cast<unsigned long long>(mask),e,mode>=2?"HIGH":"ABOVE_NORMAL",okAffinity?"":" [affinity FAILED]",okPriority?"":" [priority FAILED]");
    Log("CpuAffinity: %s\n",g_status);
    g_applied=mode;
}
const char* Status(){return g_status;}
}
