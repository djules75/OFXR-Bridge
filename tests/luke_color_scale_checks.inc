// Independent CPU area/bilinear oracle for both eyes' private colour raster.
namespace quality {
void color_scale_checks(ID3D12Device* device,ID3D12CommandQueue* queue) {
    constexpr UINT w=37,h=29;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width=w;rd.Height=h;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D12Resource> source;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&source)),"scale source");
    const auto buffer=[&](UINT64 size,D3D12_HEAP_TYPE type) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> out;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out)),"scale buffer");return out;
    };
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"scale allocator");
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"scale list");
    ComPtr<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"scale fence");HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 value{};
    const auto submit=[&]{check(list->Close(),"scale close");ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);check(queue->Signal(fence.Get(),++value),"scale signal");check(fence->SetEventOnCompletion(value,event),"scale event");require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"scale timeout");};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size{};device->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,nullptr,&size);
    auto upload=buffer(size,D3D12_HEAP_TYPE_UPLOAD),read=buffer(size,D3D12_HEAP_TYPE_READBACK);void* data{};check(upload->Map(0,nullptr,&data),"scale map");
    std::vector<unsigned char> original(w*h*4);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)for(UINT c=0;c<4;++c)original[(y*w+x)*4+c]=static_cast<unsigned char>((x*37+y*71+c*53)%256);
    for(UINT y=0;y<h;++y)std::memcpy(static_cast<char*>(data)+fp.Offset+y*fp.Footprint.RowPitch,original.data()+y*w*4,w*4);upload->Unmap(0,nullptr);
    auto b=transition(source.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;to.pResource=source.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);submit();
    for(UINT factor:{2u,4u}) {
        const UINT lw=(w+factor-1)/factor,lh=(h+factor-1)/factor;
        rd.Width=lw;rd.Height=lh;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> reduced_texture;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&reduced_texture)),"scale reduced_texture");
        xrfg::AfwColorScale scale;check(scale.prepare(device,w,h,rd.Format),"scale pipeline");check(allocator->Reset(),"scale reset");check(list->Reset(allocator.Get(),nullptr),"scale reset list");
        scale.record(device,list.Get(),source.Get(),D3D12_RESOURCE_STATE_COMMON,w,h,reduced_texture.Get(),D3D12_RESOURCE_STATE_COMMON,lw,lh,0);
        scale.record(device,list.Get(),reduced_texture.Get(),D3D12_RESOURCE_STATE_COMMON,lw,lh,scale.expanded(),D3D12_RESOURCE_STATE_COMMON,w,h,1);
        b=transition(scale.expanded(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);from.pResource=scale.expanded();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=read.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);submit();
        std::vector<double> reduced(lw*lh*4);
        for(UINT y=0;y<lh;++y)for(UINT x=0;x<lw;++x)for(UINT c=0;c<4;++c) {
            const double x0=double(x)*w/lw,x1=double(x+1)*w/lw,y0=double(y)*h/lh,y1=double(y+1)*h/lh;double sum{};
            for(int sy=int(std::floor(y0));sy<int(std::ceil(y1));++sy)for(int sx=int(std::floor(x0));sx<int(std::ceil(x1));++sx)
                sum+=original[(sy*w+sx)*4+c]*(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)));
            reduced[(y*lw+x)*4+c]=std::round(sum/((x1-x0)*(y1-y0)));
        }
        check(read->Map(0,nullptr,&data),"scale read");double max_error{};
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)for(UINT c=0;c<4;++c) {
            const double px=(x+.5)*lw/w-.5,py=(y+.5)*lh/h-.5;const int ix=int(std::floor(px)),iy=int(std::floor(py));double expected{};
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                const auto sx=std::clamp(ix+dx,0,int(lw)-1),sy=std::clamp(iy+dy,0,int(lh)-1);
                expected+=reduced[(sy*lw+sx)*4+c]*(dx?px-ix:1-(px-ix))*(dy?py-iy:1-(py-iy));
            }
            auto actual=static_cast<unsigned char*>(data)[fp.Offset+y*fp.Footprint.RowPitch+x*4+c];max_error=std::max(max_error,std::abs(actual-expected));
        }
        read->Unmap(0,nullptr);require(max_error<=1.6,"colour scale phase/edge/area oracle mismatch");
        std::cout<<"Colour area/bilinear factor="<<factor<<" odd37x29 all RGBA/edge pixels max_error="<<max_error<<" PASS\n";
    }
    CloseHandle(event);
}
}
