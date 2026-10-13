#include "xrfg/vulkan_command_stream.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>
#include <future>
#include <thread>
#include <chrono>

using namespace xrfg::native_vulkan;
namespace {
struct Object {void* dispatch;};
int dispatch_identity;Object device_object{&dispatch_identity},queue_object{&dispatch_identity};
std::vector<std::unique_ptr<Object>> objects;
VkDevice device=reinterpret_cast<VkDevice>(&device_object);
VkQueue queue=reinterpret_cast<VkQueue>(&queue_object);
VkCommandPool pool=reinterpret_cast<VkCommandPool>(uintptr_t(1));
struct Command {const char* kind;VkCommandBuffer handle;uint32_t value;};
std::vector<Command> events;
struct Submission {
    std::vector<VkCommandBuffer> commands;
    std::vector<VkSemaphore> waits,signals;
    std::vector<uint64_t> wait_values,signal_values;
};
std::vector<Submission> submitted;VkFence submitted_fence{};
VkResult driver_submit_result=VK_SUCCESS;
unsigned driver_submissions{},submitted_callbacks{},abandoned_callbacks{};
unsigned released_recording_resources{};
VkCommandBuffer callback_original{};std::atomic<bool> callbacks_unlock_commands{true};
void probe_callback_locks() noexcept {
    try {
        auto done=std::make_shared<std::promise<void>>();auto future=done->get_future();const auto command=callback_original;
        std::thread([done,command]{(void)device_for(command);done->set_value();}).detach();
        if(future.wait_for(std::chrono::seconds(1))!=std::future_status::ready)callbacks_unlock_commands=false;
    }catch(...){callbacks_unlock_commands=false;}
}
struct CallbackContext {unsigned id{};};
void submitted_callback(VkQueue submitted_queue,void* opaque) noexcept {
    probe_callback_locks();
    auto* context=static_cast<CallbackContext*>(opaque);
    if(submitted_queue==queue&&context)++submitted_callbacks;
    events.push_back({"submit-callback",{},context?context->id:0});
}
void abandoned_callback(void* opaque) noexcept {
    probe_callback_locks();
    if(opaque)++abandoned_callbacks;
}
CommandHandoff handoff(VkSemaphore timeline,uint64_t prefix,uint64_t ready,uint64_t complete,unsigned id) {
    CommandHandoff result{timeline,prefix,ready,complete};
    result.submission_context=std::make_shared<CallbackContext>(CallbackContext{id});
    result.submitted=submitted_callback;result.abandoned=abandoned_callback;return result;
}
VKAPI_ATTR VkResult VKAPI_CALL create_pool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out){*out=pool;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* out) {
    for(uint32_t i=0;i<info->commandBufferCount;++i){objects.emplace_back(std::make_unique<Object>(Object{&dispatch_identity}));
        out[i]=reinterpret_cast<VkCommandBuffer>(objects.back().get());}return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer handle,const VkCommandBufferBeginInfo*){events.push_back({"begin",handle,0});return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer handle){events.push_back({"end",handle,0});return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL reset(VkCommandBuffer,VkCommandBufferResetFlags){return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL free_commands(VkDevice,VkCommandPool,uint32_t count,const VkCommandBuffer* commands){for(uint32_t i=0;i<count;++i)events.push_back({"free",commands[i],0});}
VKAPI_ATTR VkResult VKAPI_CALL reset_pool(VkDevice,VkCommandPool,VkCommandPoolResetFlags){return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice,VkCommandPool,const VkAllocationCallbacks*){}
VKAPI_ATTR void VKAPI_CALL bind_pipeline(VkCommandBuffer handle,VkPipelineBindPoint bind_point,VkPipeline){
    events.push_back({bind_point==VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR?"rt-pipeline":"pipeline",handle,0});}
VKAPI_ATTR void VKAPI_CALL push_constants(VkCommandBuffer handle,VkPipelineLayout,VkShaderStageFlags,uint32_t,uint32_t size,const void* bytes) {
    events.push_back({"push",handle,size?static_cast<const uint32_t*>(bytes)[0]:0});
}
VKAPI_ATTR void VKAPI_CALL line_width(VkCommandBuffer handle,float width){events.push_back({"line",handle,uint32_t(width)});}
VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer handle,uint32_t x,uint32_t,uint32_t){events.push_back({"dispatch",handle,x});}
VKAPI_ATTR void VKAPI_CALL descriptors(VkCommandBuffer handle,VkPipelineBindPoint bind_point,VkPipelineLayout,uint32_t,uint32_t,
    const VkDescriptorSet*,uint32_t count,const uint32_t* offsets){
    events.push_back({bind_point==VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR?"rt-descriptor":"descriptor",handle,count?offsets[0]:0});}
VKAPI_ATTR void VKAPI_CALL viewports(VkCommandBuffer handle,uint32_t count,const VkViewport* views){
    events.push_back({"viewport-count",handle,count?uint32_t(views[0].x):0});}
VKAPI_ATTR void VKAPI_CALL vertex2(VkCommandBuffer handle,uint32_t,uint32_t count,const VkBuffer*,const VkDeviceSize* offsets,
    const VkDeviceSize*,const VkDeviceSize* strides){events.push_back({"vertex2",handle,count?uint32_t(offsets[0]+(strides?strides[0]:0)):0});}
VKAPI_ATTR void VKAPI_CALL begin_rendering(VkCommandBuffer handle,const VkRenderingInfo*){events.push_back({"rendering",handle,0});}
VKAPI_ATTR void VKAPI_CALL end_rendering(VkCommandBuffer handle){events.push_back({"rendering-end",handle,0});}
VKAPI_ATTR void VKAPI_CALL descriptor_buffers(VkCommandBuffer,uint32_t,const VkDescriptorBufferBindingInfoEXT*){}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue,uint32_t count,const VkSubmitInfo* info,VkFence fence) {
    ++driver_submissions;if(driver_submit_result!=VK_SUCCESS)return driver_submit_result;
    submitted.clear();submitted_fence=fence;
    for(uint32_t i=0;i<count;++i) {
        Submission s;s.commands.assign(info[i].pCommandBuffers,info[i].pCommandBuffers+info[i].commandBufferCount);
        if(info[i].waitSemaphoreCount)s.waits.assign(info[i].pWaitSemaphores,info[i].pWaitSemaphores+info[i].waitSemaphoreCount);
        if(info[i].signalSemaphoreCount)s.signals.assign(info[i].pSignalSemaphores,info[i].pSignalSemaphores+info[i].signalSemaphoreCount);
        for(auto* node=static_cast<const VkBaseInStructure*>(info[i].pNext);node;node=node->pNext) {
            if(node->sType==VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
                auto* t=reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(node);
                if(t->waitSemaphoreValueCount)s.wait_values.assign(t->pWaitSemaphoreValues,t->pWaitSemaphoreValues+t->waitSemaphoreValueCount);
                if(t->signalSemaphoreValueCount)s.signal_values.assign(t->pSignalSemaphoreValues,t->pSignalSemaphoreValues+t->signalSemaphoreValueCount);
            }
        }submitted.push_back(std::move(s));
    }return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL submit2(VkQueue,uint32_t count,const VkSubmitInfo2* info,VkFence fence) {
    ++driver_submissions;if(driver_submit_result!=VK_SUCCESS)return driver_submit_result;
    submitted.clear();submitted_fence=fence;
    for(uint32_t i=0;i<count;++i) {
        Submission s;for(uint32_t j=0;j<info[i].commandBufferInfoCount;++j)s.commands.push_back(info[i].pCommandBufferInfos[j].commandBuffer);
        for(uint32_t j=0;j<info[i].waitSemaphoreInfoCount;++j){s.waits.push_back(info[i].pWaitSemaphoreInfos[j].semaphore);s.wait_values.push_back(info[i].pWaitSemaphoreInfos[j].value);}
        for(uint32_t j=0;j<info[i].signalSemaphoreInfoCount;++j){s.signals.push_back(info[i].pSignalSemaphoreInfos[j].semaphore);s.signal_values.push_back(info[i].pSignalSemaphoreInfos[j].value);}
        submitted.push_back(std::move(s));
    }return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name) {
#define MATCH(vk_name,function) if(!strcmp(name,#vk_name))return reinterpret_cast<PFN_vkVoidFunction>(&function)
    MATCH(vkCreateCommandPool,create_pool);MATCH(vkAllocateCommandBuffers,allocate);MATCH(vkBeginCommandBuffer,begin);
    MATCH(vkEndCommandBuffer,end);MATCH(vkResetCommandBuffer,reset);MATCH(vkFreeCommandBuffers,free_commands);
    MATCH(vkResetCommandPool,reset_pool);MATCH(vkDestroyCommandPool,destroy_pool);MATCH(vkCmdBindPipeline,bind_pipeline);
    MATCH(vkCmdPushConstants,push_constants);MATCH(vkCmdSetLineWidth,line_width);MATCH(vkCmdDispatch,dispatch);
    MATCH(vkCmdBindDescriptorSets,descriptors);MATCH(vkQueueSubmit,submit);MATCH(vkQueueSubmit2,submit2);MATCH(vkQueueSubmit2KHR,submit2);
    MATCH(vkCmdBeginRendering,begin_rendering);MATCH(vkCmdEndRendering,end_rendering);
    MATCH(vkCmdBindDescriptorBuffersEXT,descriptor_buffers);
    MATCH(vkCmdSetViewportWithCount,viewports);MATCH(vkCmdBindVertexBuffers2,vertex2);
#undef MATCH
    return nullptr;
}
template<class T>T hooked(const char* name){return reinterpret_cast<T>(resolve_command(device,name,get(device,name)));}
VkResult copyback(VkCommandBuffer suffix,void*){events.push_back({"copyback",suffix,0});return VK_SUCCESS;}
}
int main() {
    unsigned checks{},failures{};auto check=[&](bool value,const char* label){++checks;if(!value){++failures;printf("FAIL %s\n",label);}};
    check(initialize_commands({}, {},device,nullptr,get),"per-device raw dispatcher initialized");
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=3;
    check(hooked<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device,&cp,nullptr,&pool)==VK_SUCCESS,"pool family retained");
    VkCommandBuffer command{};VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool=pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
    check(hooked<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device,&allocation,&command)==VK_SUCCESS,"native original allocation registered");
    callback_original=command;
    check(device_for(command)==device&&device_for(reinterpret_cast<VkCommandBuffer>(&queue_object))==VK_NULL_HANDLE,
        "only observed original allocations identify native feature device");
    check(queue_family_for(command)==3&&queue_family_for(reinterpret_cast<VkCommandBuffer>(&queue_object))==UINT32_MAX,
        "external resource ownership uses observed command pool family instead of XR queue family");
    VkCommandBufferBeginInfo begun{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begun.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    auto begin_hook=hooked<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");begin_hook(command,&begun);
    check(can_split(command)&&recording_epoch(command)!=0,"exact recording preflight");auto epoch=recording_epoch(command);
    auto retained=std::shared_ptr<void>(new unsigned(1),[](void* value){delete static_cast<unsigned*>(value);
        ++released_recording_resources;probe_callback_locks();});
    std::weak_ptr<void> retained_identity=retained;
    check(retain_recording(command,retained),"raw capture resources attach before any split or prefix copy");retained.reset();
    hooked<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_COMPUTE,reinterpret_cast<VkPipeline>(uintptr_t(2)));
    uint32_t source_bytes=17;auto layout=reinterpret_cast<VkPipelineLayout>(uintptr_t(3));
    hooked<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&source_bytes);
    uint32_t dynamic_offset=31;auto descriptor=reinterpret_cast<VkDescriptorSet>(uintptr_t(4));
    hooked<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&descriptor,1,&dynamic_offset);
    hooked<PFN_vkCmdSetLineWidth>("vkCmdSetLineWidth")(command,3.f);
    hooked<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,reinterpret_cast<VkPipeline>(uintptr_t(9)));
    uint32_t ray_offset=43;hooked<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,layout,0,1,&descriptor,1,&ray_offset);
    uint32_t ray_bytes=47;hooked<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,layout,VK_SHADER_STAGE_RAYGEN_BIT_KHR,0,4,&ray_bytes);
    VkViewport viewport{};viewport.x=53;hooked<PFN_vkCmdSetViewportWithCount>("vkCmdSetViewportWithCount")(command,1,&viewport);
    auto vertex_buffer=reinterpret_cast<VkBuffer>(uintptr_t(10));VkDeviceSize vertex_offset=59,vertex_size=128,vertex_stride=61;
    hooked<PFN_vkCmdBindVertexBuffers2>("vkCmdBindVertexBuffers2")(command,0,1,&vertex_buffer,&vertex_offset,&vertex_size,&vertex_stride);
    ray_offset=99;ray_bytes=99;viewport.x=99;vertex_offset=99;vertex_stride=99;
    source_bytes=99;dynamic_offset=99;
    auto timeline=reinterpret_cast<VkSemaphore>(uintptr_t(5));
    auto first=split(command,handoff(timeline,100,101,102,1),copyback,nullptr);
    check(first.result==VK_SUCCESS&&first.continuation&&first.recording_epoch==epoch,"first native split preserves epoch");
    check(native(command)==first.continuation,"native SR remaps to actual suffix");
    auto event_index=[&](const char* kind,VkCommandBuffer cmd,uint32_t value) {
        for(size_t i=0;i<events.size();++i)if(!strcmp(events[i].kind,kind)&&events[i].handle==cmd&&events[i].value==value)return i;
        return events.size();
    };
    check(event_index("push",first.continuation,17)<events.size(),"push constant snapshot owns caller bytes");
    check(event_index("descriptor",first.continuation,31)<events.size(),"dynamic offsets retain immutable caller values");
    check(event_index("rt-pipeline",first.continuation,0)<events.size()&&event_index("rt-descriptor",first.continuation,43)<events.size()&&
        event_index("push",first.continuation,47)<events.size(),"ray tracing pipeline descriptors and push constants retain independent native state");
    check(event_index("viewport-count",first.continuation,53)<events.size()&&event_index("vertex2",first.continuation,120)<events.size(),
        "Vulkan1.3 dynamic viewport and vertex stride own immutable caller arrays");
    check(event_index("copyback",first.continuation,0)<event_index("pipeline",first.continuation,0),"copyback precedes restored native bindings");
    auto dispatch_hook=hooked<PFN_vkCmdDispatch>("vkCmdDispatch");dispatch_hook(command,7,1,1);
    check(event_index("dispatch",first.continuation,7)<events.size(),"cached vkCmd wrapper follows suffix");
    auto second=split(command,handoff(timeline,103,104,105,2),copyback,nullptr);
    check(second.result==VK_SUCCESS&&native(command)==second.continuation,"second SR split chains same timeline");
    dispatch_hook(first.continuation,11,1,1);
    check(event_index("dispatch",second.continuation,11)<events.size()&&device_for(first.continuation)==device&&
        queue_family_for(first.continuation)==queue_family_for(command)&&recording_epoch(first.continuation)==epoch,
        "outer wrapper received suffix follows exact inner continuation and owner");
    check(submitted_callbacks==0&&first.lifetime->disposition==RecordingDisposition::recording&&
        second.lifetime->epoch==epoch,"recording never schedules an unsignaled D3D12 prefix wait");
    hooked<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command);
    check(std::count_if(events.begin(),events.end(),[&](const Command& e){return !strcmp(e.kind,"end")&&e.handle==command;})==1,
        "application end does not end original prefix twice");
    auto app_wait=reinterpret_cast<VkSemaphore>(uintptr_t(6)),app_signal=reinterpret_cast<VkSemaphore>(uintptr_t(7));
    uint64_t wait_value=8,signal_value=9;VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.waitSemaphoreValueCount=1;values.pWaitSemaphoreValues=&wait_value;values.signalSemaphoreValueCount=1;values.pSignalSemaphoreValues=&signal_value;
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.pNext=&values;info.commandBufferCount=1;info.pCommandBuffers=&command;info.waitSemaphoreCount=1;info.pWaitSemaphores=&app_wait;
    info.pWaitDstStageMask=&stage;info.signalSemaphoreCount=1;info.pSignalSemaphores=&app_signal;
    auto app_fence=reinterpret_cast<VkFence>(uintptr_t(8));
    check(hooked<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&info,app_fence)==VK_SUCCESS&&submitted.size()==3,"Submit1 chains prefix and both suffixes");
    check(submitted.front().waits==std::vector<VkSemaphore>{app_wait}&&submitted.front().wait_values==std::vector<uint64_t>{8},"original waits stay on first prefix");
    check(submitted[1].wait_values==std::vector<uint64_t>{101}&&submitted[1].signal_values==std::vector<uint64_t>{103},"intermediate suffix orders second prefix capture");
    check(submitted.back().wait_values==std::vector<uint64_t>{104}&&submitted.back().signals==std::vector<VkSemaphore>{app_signal,timeline}&&
        submitted.back().signal_values==std::vector<uint64_t>{9,105},"final suffix retains application signal and monotonic native completion");
    check(submitted_fence==app_fence&&wait_value==8&&signal_value==9&&values.signalSemaphoreValueCount==1,"application fence and const metadata preserved");
    check(submitted_callbacks==2&&first.lifetime->disposition==RecordingDisposition::submitted&&
        second.lifetime->disposition==RecordingDisposition::submitted,"successful real submit activates each exact capture once");
    check(!retained_identity.expired()&&released_recording_resources==0,
        "successful submission retains source-recording resources until application disposal");
    check(hooked<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&info,app_fence)==VK_ERROR_FEATURE_NOT_PRESENT,"one-time native split cannot silently resubmit stale timeline");
    hooked<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0);begin_hook(command,&begun);
    check(recording_epoch(command)>epoch&&native(command)==command,"reset invalidates old suffix and advances epoch");
    check(!device_for(first.continuation)&&native(first.continuation)==first.continuation,
        "reset removes exact recording continuation aliases");
    check(retained_identity.expired()&&released_recording_resources==1,
        "exact original reset releases resources after command mutex unlock");
    check(abandoned_callbacks==0,"reset of already submitted recording cannot cancel GPU ownership");
    auto begin_render=hooked<PFN_vkCmdBeginRendering>("vkCmdBeginRendering");VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};begin_render(command,&rendering);
    const auto event_count=events.size();
    check(!can_split(command)&&split(command,{timeline,106,107,108}).result==VK_ERROR_FEATURE_NOT_PRESENT&&
        events.size()==event_count&&native(command)==command,"active rendering split refuses without mutating original recording");
    hooked<PFN_vkCmdEndRendering>("vkCmdEndRendering")(command);
    auto third=split(command,{timeline,106,107,108},copyback,nullptr);hooked<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command);
    VkCommandBufferSubmitInfo submitted_command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};submitted_command.commandBuffer=command;submitted_command.deviceMask=1;
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};wait.semaphore=app_wait;wait.value=8;wait.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signal=wait;signal.semaphore=app_signal;signal.value=9;
    VkSubmitInfo2 info2{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};info2.commandBufferInfoCount=1;info2.pCommandBufferInfos=&submitted_command;
    info2.waitSemaphoreInfoCount=1;info2.pWaitSemaphoreInfos=&wait;info2.signalSemaphoreInfoCount=1;info2.pSignalSemaphoreInfos=&signal;
    check(third.result==VK_SUCCESS&&hooked<PFN_vkQueueSubmit2KHR>("vkQueueSubmit2KHR")(queue,1,&info2,app_fence)==VK_SUCCESS&&submitted.size()==2,"Submit2KHR preserves split schedule");
    check(submitted.front().wait_values==std::vector<uint64_t>{8}&&submitted.back().signal_values==std::vector<uint64_t>{9,108},"Submit2 metadata remains on prefix/final suffix");
    check(info2.commandBufferInfoCount==1&&submitted_command.commandBuffer==command&&signal.value==9,"Submit2 caller memory is immutable");
    hooked<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0);begin_hook(command,&begun);
    VkDescriptorBufferBindingInfoEXT unknown_binding{VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT};
    hooked<PFN_vkCmdBindDescriptorBuffersEXT>("vkCmdBindDescriptorBuffersEXT")(command,1,&unknown_binding);
    auto failed_resources=std::shared_ptr<void>(new unsigned(2),[](void* value){delete static_cast<unsigned*>(value);
        ++released_recording_resources;probe_callback_locks();});
    std::weak_ptr<void> failed_identity=failed_resources;
    check(retain_recording(command,failed_resources),"recording can retain resources even if a later split is refused");failed_resources.reset();
    const auto unreplayable_events=events.size();
    check(!can_split(command)&&split(command,{timeline,109,110,111}).result==VK_ERROR_FEATURE_NOT_PRESENT&&
        events.size()==unreplayable_events&&native(command)==command,"unreplayable binding refuses split without any allocation or end");
    check(!failed_identity.expired(),"failed unsplit capture cannot release resources still referenced by native prefix");
    hooked<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0);begin_hook(command,&begun);
    check(failed_identity.expired()&&released_recording_resources==2,"failed unsplit capture retires only on exact original reset");
    auto discarded=split(command,handoff(timeline,109,110,111,3));
    check(discarded.result==VK_SUCCESS&&submitted_callbacks==2,"unsubmitted recording retains inert capture context");
    hooked<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0);
    check(discarded.lifetime->disposition==RecordingDisposition::abandoned&&abandoned_callbacks==1,
        "reset precisely cancels detached recording without queue work");
    begin_hook(command,&begun);auto failed=split(command,handoff(timeline,112,113,114,4));
    hooked<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command);
    driver_submit_result=VK_ERROR_DEVICE_LOST;
    check(hooked<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&info,app_fence)==VK_ERROR_DEVICE_LOST&&
        submitted_callbacks==2&&failed.lifetime->disposition==RecordingDisposition::recording,
        "failed real driver submit cannot activate a D3D12 wait or steal recording");
    driver_submit_result=VK_SUCCESS;hooked<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command,0);
    check(failed.lifetime->disposition==RecordingDisposition::abandoned&&abandoned_callbacks==2,
        "failed submission later reset still cancels exact recording");
    begin_hook(command,&begun);auto unknown_metadata=split(command,handoff(timeline,115,116,117,5));
    hooked<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command);
    VkBaseInStructure unknown_node{};unknown_node.sType=static_cast<VkStructureType>(0x7ffffffd);
    info.pNext=&unknown_node;const auto old_submissions=driver_submissions;
    check(hooked<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&info,app_fence)==VK_ERROR_FEATURE_NOT_PRESENT&&
        driver_submissions==old_submissions&&submitted_callbacks==2&&info.pNext==&unknown_node,
        "unknown submit metadata refuses before raw driver or ownership mutation");
    destroy_commands(device);
    check(callbacks_unlock_commands,"submit and abandonment callbacks permit concurrent command-state access without lock inversion");
    printf("Native Vulkan command contracts: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
