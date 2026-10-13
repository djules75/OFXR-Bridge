#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace xrfg {
// Resolve the module's own executable export from its matching on-disk PE.
// Injectors may redirect GetProcAddress or the live EAT to a forwarding shim;
// the declared native body is also called through their cached original pointer.
// Forwarded exports and changed/malformed images never provide provenance.
inline FARPROC native_export_address(HMODULE module, const char* name) noexcept {
    struct Handles {
        HANDLE file{INVALID_HANDLE_VALUE}, mapping{}; void* view{};
        ~Handles() {if(view)UnmapViewOfFile(view);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    } handles;
    try {
        if(!module||!name)return nullptr;
        std::wstring path(32768,L'\0');
        const auto length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        if(!length||length>=path.size())return nullptr;path.resize(length);
        handles.file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handles.file==INVALID_HANDLE_VALUE)return nullptr;
        LARGE_INTEGER size{};if(!GetFileSizeEx(handles.file,&size)||size.QuadPart<sizeof(IMAGE_DOS_HEADER)||size.QuadPart>128*1024*1024)return nullptr;
        handles.mapping=CreateFileMappingW(handles.file,nullptr,PAGE_READONLY,0,0,nullptr);
        if(!handles.mapping)return nullptr;
        handles.view=MapViewOfFile(handles.mapping,FILE_MAP_READ,0,0,0);if(!handles.view)return nullptr;
        const auto* bytes=static_cast<const std::byte*>(handles.view);
        const auto range=[&](std::size_t offset,std::size_t count){return offset<=static_cast<std::size_t>(size.QuadPart)&&count<=static_cast<std::size_t>(size.QuadPart)-offset;};
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||!range(dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64)))return nullptr;
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
            nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64)||
            nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXPORT||nt->FileHeader.NumberOfSections>96)return nullptr;
        const auto* live_dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if(live_dos->e_magic!=IMAGE_DOS_SIGNATURE||live_dos->e_lfanew!=dos->e_lfanew)return nullptr;
        const auto* live_nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const std::byte*>(module)+live_dos->e_lfanew);
        if(live_nt->Signature!=IMAGE_NT_SIGNATURE||live_nt->FileHeader.TimeDateStamp!=nt->FileHeader.TimeDateStamp||
            live_nt->OptionalHeader.SizeOfImage!=nt->OptionalHeader.SizeOfImage)return nullptr;
        const auto* sections=IMAGE_FIRST_SECTION(nt);
        if(!range(reinterpret_cast<const std::byte*>(sections)-bytes,nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)))return nullptr;
        const auto rva=[&](DWORD value,std::size_t count)->const std::byte* {
            if(value<nt->OptionalHeader.SizeOfHeaders)return range(value,count)&&count<=nt->OptionalHeader.SizeOfHeaders-value?bytes+value:nullptr;
            for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
                const auto& s=sections[i];
                if(value>=s.VirtualAddress&&value-s.VirtualAddress<s.SizeOfRawData) {
                    const auto offset=value-s.VirtualAddress;
                    const auto raw=static_cast<std::size_t>(s.PointerToRawData)+offset;
                    return count<=s.SizeOfRawData-offset&&range(raw,count)?bytes+raw:nullptr;
                }
            }return nullptr;
        };
        const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        const auto* data=rva(directory.VirtualAddress,sizeof(IMAGE_EXPORT_DIRECTORY));if(!data)return nullptr;
        const auto* exports=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(data);
        if(exports->NumberOfNames>65536||exports->NumberOfFunctions>65536)return nullptr;
        const auto* names=reinterpret_cast<const DWORD*>(rva(exports->AddressOfNames,exports->NumberOfNames*sizeof(DWORD)));
        const auto* ordinals=reinterpret_cast<const WORD*>(rva(exports->AddressOfNameOrdinals,exports->NumberOfNames*sizeof(WORD)));
        const auto* functions=reinterpret_cast<const DWORD*>(rva(exports->AddressOfFunctions,exports->NumberOfFunctions*sizeof(DWORD)));
        if(!names||!ordinals||!functions)return nullptr;
        const auto wanted=std::strlen(name);if(wanted>1024)return nullptr;
        for(DWORD i=0;i<exports->NumberOfNames;++i) {
            const auto* candidate=rva(names[i],wanted+1);
            if(!candidate||std::memcmp(candidate,name,wanted+1)||ordinals[i]>=exports->NumberOfFunctions)continue;
            const auto address=functions[ordinals[i]];
            if(!address||address>=nt->OptionalHeader.SizeOfImage||
                (address>=directory.VirtualAddress&&static_cast<std::uint64_t>(address)-directory.VirtualAddress<directory.Size))return nullptr;
            for(unsigned j=0;j<nt->FileHeader.NumberOfSections;++j) {
                const auto& s=sections[j];
                if(address>=s.VirtualAddress&&address-s.VirtualAddress<s.Misc.VirtualSize&&
                    (s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&rva(address,1))
                    return reinterpret_cast<FARPROC>(reinterpret_cast<std::byte*>(module)+address);
            }return nullptr;
        }
    }catch(...){}
    return nullptr;
}
} // namespace xrfg
