#include "xrfg/native_ingress/DeadSpaceCameraSource.h"
#include <iostream>
#include <stdexcept>
#include <limits>
namespace source=dead_space_camera_source;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Fixture {
    std::array<unsigned char,source::packet_bytes> packet{};
    source::Camera camera;
    template<std::size_t N>void put(std::size_t offset,const std::array<float,N>& value){std::memcpy(packet.data()+offset,value.data(),sizeof(value));}
    Fixture(){
        camera.frame=317;camera.observed=true;camera.rejection=8;
        camera.position={7,8,9};camera.right={1,0,0};camera.up={0,1,0};camera.forward={-1,0,0};
        camera.projection={1,0,0,0,0,2,0,0,0,0,0,-1,0,0,.1F,0};
        camera.inverse={1,0,0,0,0,.5F,0,0,0,0,0,10,0,0,-1,0};
        camera.near_z=.1F;camera.far_z=100;camera.fov=1;camera.camera_motion=1;camera.inverted=true;
        put(0x70,camera.position);put(0x7c,camera.up);put(0x94,camera.right);put(0x88,std::array<float,3>{0,0,1});
        put(0xa0,camera.projection);put(0xe0,camera.inverse);put(0x120,camera.previous);put(0x160,camera.to_current);
    }
};
volatile LONG calls{};const void* original_packet{};std::uint32_t original_frame{};source::Camera emitted;bool corrected{};
__declspec(noinline) void original_builder(const void* packet,std::uint32_t frame) {
    ++calls;original_packet=packet;original_frame=frame;corrected=source::correct(emitted);
}
int main(){try{
    Fixture f;const auto original=f.packet;
    {source::Scope scope(f.packet.data(),f.camera.frame);auto c=f.camera;
        require(source::correct(c)&&c.valid&&c.rejection==0&&c.forward==std::array<float,3>{0,0,-1},"native original third axis unavailable");
        require(c.right==f.camera.right&&c.up==f.camera.up&&c.position==f.camera.position&&c.projection==f.camera.projection&&
            c.inverse==f.camera.inverse&&c.previous==f.camera.previous&&c.frame==f.camera.frame&&c.viewport==0,"non-forward native metadata changed");
        require(f.packet==original,"application source modified");}
    require(!source::source.active&&!source::correct(f.camera),"native source escaped invocation scope");
    auto reject=[](Fixture x,unsigned expected,const char* why,std::uint32_t frame=317){
        auto before=x.camera;source::Scope scope(x.packet.data(),frame);require(!source::correct(x.camera)&&
            !std::memcmp(&before,&x.camera,sizeof(before))&&source::statistics().failure==expected,why);};
    reject(f,2,"different source frame accepted",318);
    auto bad=f;bad.camera.viewport=1;reject(bad,2,"viewport invented");
    bad=f;bad.camera.ambiguous=true;reject(bad,4,"ambiguous SDK receipt accepted");
    bad=f;bad.camera.rejection=24;reject(bad,4,"another invalid field repaired");
    bad=f;bad.camera.position[0]+=.1F;reject(bad,8,"source identity was only approximate");
    bad=f;bad.packet[0xa0]^=1;reject(bad,8,"unmatched native projection accepted");
    bad=f;bad.put(0x88,std::array<float,3>{1,0,0});reject(bad,16,"collinear native axes accepted");
    bad=f;bad.put(0x88,std::array<float,3>{0,0,-1});reject(bad,16,"wrong-handed source accepted");
    bad=f;bad.camera.projection[0]*=2;bad.put(0xa0,bad.camera.projection);reject(bad,32,"inconsistent inverse accepted");
    bad=f;bad.camera.forward={1,0,0};reject(bad,64,"unmeasured conversion defect repaired");
    bad=f;bad.camera.forward[0]=std::numeric_limits<float>::quiet_NaN();reject(bad,64,"nonfinite SDK forward accepted as measured defect");
    {source::Scope invalid(reinterpret_cast<const void*>(1),317);auto c=f.camera;require(!source::correct(c)&&source::statistics().failure==1,"unreadable source accepted");}
    {source::Scope outer(f.packet.data(),317);{source::Scope inner(f.packet.data(),999);require(source::source.frame==999,"nested source");}
        auto c=f.camera;require(source::correct(c)&&source::source.frame==317,"outer native source not restored");}
    // Real inline hook/trampoline: preserve original arguments and execution,
    // while making only that original builder's source available to SL decode.
    auto* entry=reinterpret_cast<void*>(&original_builder);
    std::array<unsigned char,32> code{};std::memcpy(code.data(),entry,code.size());
    const auto hash=source::detail::fingerprint(code.data(),code.size());
    require(!source::detail::attach(entry,code.size(),hash^1),"unknown code was hooked");
    require(!std::memcmp(code.data(),entry,code.size()),"failed fingerprint changed target code");
    require(source::detail::attach(entry,code.size(),hash),"verified original builder hook");
    emitted=f.camera;original_builder(f.packet.data(),317);
    require(calls==1&&original_packet==f.packet.data()&&original_frame==317&&corrected&&emitted.valid&&!source::source.active,
        "original builder/context/camera contract changed");
    emitted=f.camera;original_builder(f.packet.data(),318);
    require(calls==2&&!corrected&&!emitted.valid&&!source::source.active,"builder frame substitution accepted");
    std::cout<<"Dead Space native source: verified original hook, unchanged caller bytes/arguments, exact frame and SL packet, native third axis, strict failures and nested scope PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
