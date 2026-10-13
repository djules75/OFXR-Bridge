#include "xrfg/native_export_address.hpp"
#include <iostream>
#include <stdexcept>
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv) {
    try {
        require(argc==2,"fixture path");
        auto module=LoadLibraryA(argv[1]);require(module!=nullptr,"load fixture");
        const auto actual=xrfg::native_export_address(module,"NVSDK_NGX_D3D12_EvaluateFeature");
        const auto replacement=xrfg::native_export_address(module,"NVSDK_NGX_D3D12_ReleaseFeature");
        require(actual&&actual==GetProcAddress(module,"NVSDK_NGX_D3D12_EvaluateFeature"),"native typed export provenance");
        require(!xrfg::native_export_address(module,"AbsentExport")&&
            !xrfg::native_export_address(module,"NativeExportFixtureValue"),"absent/data export accepted as executable API");
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const std::byte*>(module)+dos->e_lfanew);
        const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        auto* table=reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(reinterpret_cast<std::byte*>(module)+directory.VirtualAddress);
        auto* entries=reinterpret_cast<DWORD*>(reinterpret_cast<std::byte*>(module)+table->AddressOfFunctions);
        DWORD* selected{};
        for(DWORD i=0;i<table->NumberOfFunctions;++i)
            if(reinterpret_cast<std::byte*>(module)+entries[i]==reinterpret_cast<std::byte*>(actual))selected=&entries[i];
        require(selected&&replacement,"fixture executable EAT entries");
        const auto saved=*selected;DWORD protection{};
        require(VirtualProtect(selected,sizeof(DWORD),PAGE_READWRITE,&protection)!=0,"fixture EAT write permission");
        *selected=static_cast<DWORD>(reinterpret_cast<std::byte*>(replacement)-reinterpret_cast<std::byte*>(module));
        DWORD unused{};VirtualProtect(selected,sizeof(DWORD),protection,&unused);
        const auto redirected=GetProcAddress(module,"NVSDK_NGX_D3D12_EvaluateFeature");
        const auto recovered=xrfg::native_export_address(module,"NVSDK_NGX_D3D12_EvaluateFeature");
        const bool correct=redirected==replacement&&recovered==actual&&
            reinterpret_cast<int(*)()>(redirected)()==17&&reinterpret_cast<int(*)()>(recovered)()==13;
        VirtualProtect(selected,sizeof(DWORD),PAGE_READWRITE,&protection);*selected=saved;
        VirtualProtect(selected,sizeof(DWORD),protection,&unused);
        require(correct,"redirected lookup hid actual cached native implementation");
        auto kernel=GetModuleHandleW(L"kernel32.dll");
        require(!xrfg::native_export_address(kernel,"AcquireSRWLockExclusive"),"forwarded system export invented native body");
        FreeLibrary(module);
        std::cout<<"Native export PASS: cached implementation survives live EAT redirection; absent, data and forwarded exports reject\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
