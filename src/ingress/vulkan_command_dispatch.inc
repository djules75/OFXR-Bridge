// Generated from accepted V33034 vkCmd signatures.
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindPipeline>(*device,"vkCmdBindPipeline"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    if(pipelineBindPoint==VK_PIPELINE_BIND_POINT_GRAPHICS||pipelineBindPoint==VK_PIPELINE_BIND_POINT_COMPUTE)
        device->tracker.OnBindPipeline(commandBuffer,pipelineBindPoint,pipeline);
    else if(pipelineBindPoint==VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
        remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,pipelineBindPoint,pipeline);});
    else device->commands[commandBuffer].unsupported_state=true;
    function(actual, pipelineBindPoint, pipeline);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetViewport>(*device,"vkCmdSetViewport"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetViewport(commandBuffer,firstViewport,viewportCount,pViewports);
    std::vector<VkViewport> saved(pViewports,pViewports+viewportCount);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,firstViewport,viewportCount,saved.data());});
    function(actual, firstViewport, viewportCount, pViewports);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetScissor>(*device,"vkCmdSetScissor"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetScissor(commandBuffer,firstScissor,scissorCount,pScissors);
    std::vector<VkRect2D> saved(pScissors,pScissors+scissorCount);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,firstScissor,scissorCount,saved.data());});
    function(actual, firstScissor, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLineWidth>(*device,"vkCmdSetLineWidth"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, lineWidth);});
    function(actual, lineWidth);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthBias>(*device,"vkCmdSetDepthBias"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);});
    function(actual, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetBlendConstants>(*device,"vkCmdSetBlendConstants"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    std::array<float,4> saved{};std::copy_n(blendConstants,4,saved.data());
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,saved.data());});
    function(actual, blendConstants);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthBounds>(*device,"vkCmdSetDepthBounds"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, minDepthBounds, maxDepthBounds);});
    function(actual, minDepthBounds, maxDepthBounds);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetStencilCompareMask>(*device,"vkCmdSetStencilCompareMask"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, faceMask, compareMask);});
    function(actual, faceMask, compareMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetStencilWriteMask>(*device,"vkCmdSetStencilWriteMask"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, faceMask, writeMask);});
    function(actual, faceMask, writeMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetStencilReference>(*device,"vkCmdSetStencilReference"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, faceMask, reference);});
    function(actual, faceMask, reference);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindDescriptorSets>(*device,"vkCmdBindDescriptorSets"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    if(pipelineBindPoint==VK_PIPELINE_BIND_POINT_GRAPHICS||pipelineBindPoint==VK_PIPELINE_BIND_POINT_COMPUTE)
        device->tracker.OnBindDescriptorSets(commandBuffer,pipelineBindPoint,layout,firstSet,descriptorSetCount,pDescriptorSets,dynamicOffsetCount,pDynamicOffsets);
    else if(pipelineBindPoint==VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
        std::vector<VkDescriptorSet> sets;if(descriptorSetCount)sets.assign(pDescriptorSets,pDescriptorSets+descriptorSetCount);
        std::vector<uint32_t> offsets;if(dynamicOffsetCount)offsets.assign(pDynamicOffsets,pDynamicOffsets+dynamicOffsetCount);
        remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,pipelineBindPoint,layout,
            firstSet,descriptorSetCount,sets.data(),dynamicOffsetCount,offsets.data());});
    }else device->commands[commandBuffer].unsupported_state=true;
    function(actual, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindIndexBuffer>(*device,"vkCmdBindIndexBuffer"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnBindIndexBuffer(commandBuffer,buffer,offset,indexType);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,buffer,offset,indexType);});
    function(actual, buffer, offset, indexType);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindVertexBuffers>(*device,"vkCmdBindVertexBuffers"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnBindVertexBuffers(commandBuffer,firstBinding,bindingCount,pBuffers,pOffsets);
    std::vector<VkBuffer> buffers;std::vector<VkDeviceSize> offsets;
    if(bindingCount){buffers.assign(pBuffers,pBuffers+bindingCount);offsets.assign(pOffsets,pOffsets+bindingCount);}
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,firstBinding,bindingCount,buffers.data(),offsets.data());});
    function(actual, firstBinding, bindingCount, pBuffers, pOffsets);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDraw>(*device,"vkCmdDraw"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, vertexCount, instanceCount, firstVertex, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndexed>(*device,"vkCmdDrawIndexed"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndirect>(*device,"vkCmdDrawIndirect"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndexedIndirect>(*device,"vkCmdDrawIndexedIndirect"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDispatch>(*device,"vkCmdDispatch"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDispatchIndirect>(*device,"vkCmdDispatchIndirect"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyBuffer>(*device,"vkCmdCopyBuffer"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, srcBuffer, dstBuffer, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyImage>(*device,"vkCmdCopyImage"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBlitImage>(*device,"vkCmdBlitImage"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyBufferToImage>(*device,"vkCmdCopyBufferToImage"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyImageToBuffer>(*device,"vkCmdCopyImageToBuffer"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdUpdateBuffer>(*device,"vkCmdUpdateBuffer"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, dstBuffer, dstOffset, dataSize, pData);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdFillBuffer>(*device,"vkCmdFillBuffer"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, dstBuffer, dstOffset, size, data);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdClearColorImage>(*device,"vkCmdClearColorImage"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, image, imageLayout, pColor, rangeCount, pRanges);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdClearDepthStencilImage>(*device,"vkCmdClearDepthStencilImage"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdClearAttachments>(*device,"vkCmdClearAttachments"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, attachmentCount, pAttachments, rectCount, pRects);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdResolveImage>(*device,"vkCmdResolveImage"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetEvent>(*device,"vkCmdSetEvent"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdResetEvent>(*device,"vkCmdResetEvent"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWaitEvents>(*device,"vkCmdWaitEvents"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPipelineBarrier>(*device,"vkCmdPipelineBarrier"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginQuery>(*device,"vkCmdBeginQuery"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, queryPool, query, flags);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndQuery>(*device,"vkCmdEndQuery"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdResetQueryPool>(*device,"vkCmdResetQueryPool"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, queryPool, firstQuery, queryCount);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteTimestamp>(*device,"vkCmdWriteTimestamp"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pipelineStage, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyQueryPoolResults>(*device,"vkCmdCopyQueryPoolResults"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushConstants>(*device,"vkCmdPushConstants"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnPushConstants(commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,layout,stageFlags,offset,size,pValues);
    const auto* bytes=static_cast<const unsigned char*>(pValues);std::vector<unsigned char> saved;
    if(size)saved.assign(bytes,bytes+size);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target,layout,stageFlags,offset,size,saved.data());});
    function(actual, layout, stageFlags, offset, size, pValues);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginRenderPass>(*device,"vkCmdBeginRenderPass"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=true;
    function(actual, pRenderPassBegin, contents);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdNextSubpass>(*device,"vkCmdNextSubpass"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, contents);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRenderPass(VkCommandBuffer commandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndRenderPass>(*device,"vkCmdEndRenderPass"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=false;
    function(actual);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDeviceMask>(*device,"vkCmdSetDeviceMask"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, deviceMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDispatchBase>(*device,"vkCmdDispatchBase"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndirectCount>(*device,"vkCmdDrawIndirectCount"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndexedIndirectCount>(*device,"vkCmdDrawIndexedIndirectCount"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginRenderPass2>(*device,"vkCmdBeginRenderPass2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=true;
    function(actual, pRenderPassBegin, pSubpassBeginInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdNextSubpass2>(*device,"vkCmdNextSubpass2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pSubpassBeginInfo, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndRenderPass2>(*device,"vkCmdEndRenderPass2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=false;
    function(actual, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetEvent2>(*device,"vkCmdSetEvent2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, event, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdResetEvent2>(*device,"vkCmdResetEvent2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWaitEvents2>(*device,"vkCmdWaitEvents2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, eventCount, pEvents, pDependencyInfos);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPipelineBarrier2>(*device,"vkCmdPipelineBarrier2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteTimestamp2>(*device,"vkCmdWriteTimestamp2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, stage, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyBuffer2>(*device,"vkCmdCopyBuffer2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyImage2>(*device,"vkCmdCopyImage2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyBufferToImage2>(*device,"vkCmdCopyBufferToImage2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyBufferToImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyImageToBuffer2>(*device,"vkCmdCopyImageToBuffer2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyImageToBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBlitImage2>(*device,"vkCmdBlitImage2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pBlitImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdResolveImage2>(*device,"vkCmdResolveImage2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pResolveImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginRendering>(*device,"vkCmdBeginRendering"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=true;
    function(actual, pRenderingInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRendering(VkCommandBuffer commandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndRendering>(*device,"vkCmdEndRendering"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=false;
    function(actual);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCullMode>(*device,"vkCmdSetCullMode"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetCullMode(commandBuffer,cullMode);
    function(actual, cullMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetFrontFace>(*device,"vkCmdSetFrontFace"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetFrontFace(commandBuffer,frontFace);
    function(actual, frontFace);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetPrimitiveTopology>(*device,"vkCmdSetPrimitiveTopology"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetPrimitiveTopology(commandBuffer,primitiveTopology);
    function(actual, primitiveTopology);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport* pViewports){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetViewportWithCount>(*device,"vkCmdSetViewportWithCount"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_viewport_count(*device,commandBuffer,function,viewportCount,pViewports);
    function(actual, viewportCount, pViewports);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D* pScissors){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetScissorWithCount>(*device,"vkCmdSetScissorWithCount"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_scissor_count(*device,commandBuffer,function,scissorCount,pScissors);
    function(actual, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindVertexBuffers2>(*device,"vkCmdBindVertexBuffers2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_vertex2(*device,commandBuffer,function,firstBinding,bindingCount,pBuffers,pOffsets,pSizes,pStrides);
    function(actual, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthTestEnable>(*device,"vkCmdSetDepthTestEnable"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthTestEnable(commandBuffer,depthTestEnable);
    function(actual, depthTestEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthWriteEnable>(*device,"vkCmdSetDepthWriteEnable"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthWriteEnable(commandBuffer,depthWriteEnable);
    function(actual, depthWriteEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthCompareOp>(*device,"vkCmdSetDepthCompareOp"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthCompareOp(commandBuffer,depthCompareOp);
    function(actual, depthCompareOp);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthBoundsTestEnable>(*device,"vkCmdSetDepthBoundsTestEnable"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthBoundsTestEnable(commandBuffer,depthBoundsTestEnable);
    function(actual, depthBoundsTestEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetStencilTestEnable>(*device,"vkCmdSetStencilTestEnable"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetStencilTestEnable(commandBuffer,stencilTestEnable);
    function(actual, stencilTestEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetStencilOp>(*device,"vkCmdSetStencilOp"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetStencilOp(commandBuffer,faceMask,failOp,passOp,depthFailOp,compareOp);
    function(actual, faceMask, failOp, passOp, depthFailOp, compareOp);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRasterizerDiscardEnable>(*device,"vkCmdSetRasterizerDiscardEnable"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, rasterizerDiscardEnable);});
    function(actual, rasterizerDiscardEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthBiasEnable>(*device,"vkCmdSetDepthBiasEnable"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, depthBiasEnable);});
    function(actual, depthBiasEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetPrimitiveRestartEnable>(*device,"vkCmdSetPrimitiveRestartEnable"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, primitiveRestartEnable);});
    function(actual, primitiveRestartEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLineStipple>(*device,"vkCmdSetLineStipple"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, lineStippleFactor, lineStipplePattern);});
    function(actual, lineStippleFactor, lineStipplePattern);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindIndexBuffer2>(*device,"vkCmdBindIndexBuffer2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, buffer, offset, size, indexType);});
    function(actual, buffer, offset, size, indexType);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSet>(*device,"vkCmdPushDescriptorSet"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSetWithTemplate>(*device,"vkCmdPushDescriptorSetWithTemplate"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, descriptorUpdateTemplate, layout, set, pData);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRenderingAttachmentLocations(VkCommandBuffer commandBuffer, const VkRenderingAttachmentLocationInfo* pLocationInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRenderingAttachmentLocations>(*device,"vkCmdSetRenderingAttachmentLocations"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pLocationInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRenderingInputAttachmentIndices(VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRenderingInputAttachmentIndices>(*device,"vkCmdSetRenderingInputAttachmentIndices"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pInputAttachmentIndexInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindDescriptorSets2(VkCommandBuffer commandBuffer, const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindDescriptorSets2>(*device,"vkCmdBindDescriptorSets2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pBindDescriptorSetsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushConstants2(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushConstants2>(*device,"vkCmdPushConstants2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pPushConstantsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSet2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetInfo* pPushDescriptorSetInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSet2>(*device,"vkCmdPushDescriptorSet2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pPushDescriptorSetInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSetWithTemplate2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSetWithTemplate2>(*device,"vkCmdPushDescriptorSetWithTemplate2"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pPushDescriptorSetWithTemplateInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoBeginCodingInfoKHR* pBeginInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginVideoCodingKHR>(*device,"vkCmdBeginVideoCodingKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pBeginInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoEndCodingInfoKHR* pEndCodingInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndVideoCodingKHR>(*device,"vkCmdEndVideoCodingKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pEndCodingInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdControlVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoCodingControlInfoKHR* pCodingControlInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdControlVideoCodingKHR>(*device,"vkCmdControlVideoCodingKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCodingControlInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDecodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoDecodeInfoKHR* pDecodeInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDecodeVideoKHR>(*device,"vkCmdDecodeVideoKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pDecodeInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRenderingKHR(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginRenderingKHR>(*device,"vkCmdBeginRenderingKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=true;
    function(actual, pRenderingInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRenderingKHR(VkCommandBuffer commandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndRenderingKHR>(*device,"vkCmdEndRenderingKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=false;
    function(actual);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDeviceMaskKHR(VkCommandBuffer commandBuffer, uint32_t deviceMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDeviceMaskKHR>(*device,"vkCmdSetDeviceMaskKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, deviceMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDispatchBaseKHR(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDispatchBaseKHR>(*device,"vkCmdDispatchBaseKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSetKHR>(*device,"vkCmdPushDescriptorSetKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSetWithTemplateKHR>(*device,"vkCmdPushDescriptorSetWithTemplateKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, descriptorUpdateTemplate, layout, set, pData);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, const VkSubpassBeginInfo* pSubpassBeginInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginRenderPass2KHR>(*device,"vkCmdBeginRenderPass2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=true;
    function(actual, pRenderPassBegin, pSubpassBeginInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo* pSubpassBeginInfo, const VkSubpassEndInfo* pSubpassEndInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdNextSubpass2KHR>(*device,"vkCmdNextSubpass2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pSubpassBeginInfo, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndRenderPass2KHR>(*device,"vkCmdEndRenderPass2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].rendering=false;
    function(actual, pSubpassEndInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndirectCountKHR>(*device,"vkCmdDrawIndirectCountKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndexedIndirectCountKHR>(*device,"vkCmdDrawIndexedIndirectCountKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetFragmentShadingRateKHR(VkCommandBuffer commandBuffer, const VkExtent2D* pFragmentSize, const VkFragmentShadingRateCombinerOpKHR combinerOps[2]){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetFragmentShadingRateKHR>(*device,"vkCmdSetFragmentShadingRateKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pFragmentSize, combinerOps);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRenderingAttachmentLocationsKHR(VkCommandBuffer commandBuffer, const VkRenderingAttachmentLocationInfo* pLocationInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRenderingAttachmentLocationsKHR>(*device,"vkCmdSetRenderingAttachmentLocationsKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pLocationInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRenderingInputAttachmentIndicesKHR(VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRenderingInputAttachmentIndicesKHR>(*device,"vkCmdSetRenderingInputAttachmentIndicesKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pInputAttachmentIndexInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEncodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoEncodeInfoKHR* pEncodeInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEncodeVideoKHR>(*device,"vkCmdEncodeVideoKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pEncodeInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo* pDependencyInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetEvent2KHR>(*device,"vkCmdSetEvent2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, event, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdResetEvent2KHR(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdResetEvent2KHR>(*device,"vkCmdResetEvent2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, event, stageMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWaitEvents2KHR(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, const VkDependencyInfo* pDependencyInfos){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWaitEvents2KHR>(*device,"vkCmdWaitEvents2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, eventCount, pEvents, pDependencyInfos);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPipelineBarrier2KHR>(*device,"vkCmdPipelineBarrier2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pDependencyInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteTimestamp2KHR>(*device,"vkCmdWriteTimestamp2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, stage, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyBuffer2KHR>(*device,"vkCmdCopyBuffer2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyImage2KHR>(*device,"vkCmdCopyImage2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyBufferToImage2KHR>(*device,"vkCmdCopyBufferToImage2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyBufferToImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyImageToBuffer2KHR>(*device,"vkCmdCopyImageToBuffer2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCopyImageToBufferInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBlitImage2KHR>(*device,"vkCmdBlitImage2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pBlitImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdResolveImage2KHR>(*device,"vkCmdResolveImage2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pResolveImageInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdTraceRaysIndirect2KHR>(*device,"vkCmdTraceRaysIndirect2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, indirectDeviceAddress);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindIndexBuffer2KHR>(*device,"vkCmdBindIndexBuffer2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, buffer, offset, size, indexType);});
    function(actual, buffer, offset, size, indexType);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLineStippleKHR(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLineStippleKHR>(*device,"vkCmdSetLineStippleKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, lineStippleFactor, lineStipplePattern);});
    function(actual, lineStippleFactor, lineStipplePattern);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindDescriptorSets2KHR(VkCommandBuffer commandBuffer, const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindDescriptorSets2KHR>(*device,"vkCmdBindDescriptorSets2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pBindDescriptorSetsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushConstants2KHR(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushConstants2KHR>(*device,"vkCmdPushConstants2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pPushConstantsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSet2KHR(VkCommandBuffer commandBuffer, const VkPushDescriptorSetInfo* pPushDescriptorSetInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSet2KHR>(*device,"vkCmdPushDescriptorSet2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pPushDescriptorSetInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSetWithTemplate2KHR(VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPushDescriptorSetWithTemplate2KHR>(*device,"vkCmdPushDescriptorSetWithTemplate2KHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pPushDescriptorSetWithTemplateInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDescriptorBufferOffsets2EXT(VkCommandBuffer commandBuffer, const VkSetDescriptorBufferOffsetsInfoEXT* pSetDescriptorBufferOffsetsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDescriptorBufferOffsets2EXT>(*device,"vkCmdSetDescriptorBufferOffsets2EXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pSetDescriptorBufferOffsetsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(VkCommandBuffer commandBuffer, const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* pBindDescriptorBufferEmbeddedSamplersInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindDescriptorBufferEmbeddedSamplers2EXT>(*device,"vkCmdBindDescriptorBufferEmbeddedSamplers2EXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pBindDescriptorBufferEmbeddedSamplersInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDebugMarkerBeginEXT>(*device,"vkCmdDebugMarkerBeginEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pMarkerInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDebugMarkerEndEXT>(*device,"vkCmdDebugMarkerEndEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDebugMarkerInsertEXT>(*device,"vkCmdDebugMarkerInsertEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pMarkerInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindTransformFeedbackBuffersEXT>(*device,"vkCmdBindTransformFeedbackBuffersEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstBinding, bindingCount, pBuffers, pOffsets, pSizes);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount, const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginTransformFeedbackEXT>(*device,"vkCmdBeginTransformFeedbackEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, firstCounterBuffer, counterBufferCount, pCounterBuffers, pCounterBufferOffsets);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer, uint32_t counterBufferCount, const VkBuffer* pCounterBuffers, const VkDeviceSize* pCounterBufferOffsets){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndTransformFeedbackEXT>(*device,"vkCmdEndTransformFeedbackEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, firstCounterBuffer, counterBufferCount, pCounterBuffers, pCounterBufferOffsets);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags, uint32_t index){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginQueryIndexedEXT>(*device,"vkCmdBeginQueryIndexedEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, queryPool, query, flags, index);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, uint32_t index){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndQueryIndexedEXT>(*device,"vkCmdEndQueryIndexedEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, queryPool, query, index);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance, VkBuffer counterBuffer, VkDeviceSize counterBufferOffset, uint32_t counterOffset, uint32_t vertexStride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndirectByteCountEXT>(*device,"vkCmdDrawIndirectByteCountEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, instanceCount, firstInstance, counterBuffer, counterBufferOffset, counterOffset, vertexStride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCuLaunchKernelNVX(VkCommandBuffer commandBuffer, const VkCuLaunchInfoNVX* pLaunchInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCuLaunchKernelNVX>(*device,"vkCmdCuLaunchKernelNVX"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pLaunchInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndirectCountAMD>(*device,"vkCmdDrawIndirectCountAMD"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawIndexedIndirectCountAMD>(*device,"vkCmdDrawIndexedIndirectCountAMD"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer, const VkConditionalRenderingBeginInfoEXT* pConditionalRenderingBegin){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginConditionalRenderingEXT>(*device,"vkCmdBeginConditionalRenderingEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pConditionalRenderingBegin);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndConditionalRenderingEXT>(*device,"vkCmdEndConditionalRenderingEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewportWScalingNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewportWScalingNV* pViewportWScalings){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetViewportWScalingNV>(*device,"vkCmdSetViewportWScalingNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstViewport, viewportCount, pViewportWScalings);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDiscardRectangleEXT(VkCommandBuffer commandBuffer, uint32_t firstDiscardRectangle, uint32_t discardRectangleCount, const VkRect2D* pDiscardRectangles){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDiscardRectangleEXT>(*device,"vkCmdSetDiscardRectangleEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstDiscardRectangle, discardRectangleCount, pDiscardRectangles);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDiscardRectangleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 discardRectangleEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDiscardRectangleEnableEXT>(*device,"vkCmdSetDiscardRectangleEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, discardRectangleEnable);});
    function(actual, discardRectangleEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDiscardRectangleModeEXT(VkCommandBuffer commandBuffer, VkDiscardRectangleModeEXT discardRectangleMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDiscardRectangleModeEXT>(*device,"vkCmdSetDiscardRectangleModeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, discardRectangleMode);});
    function(actual, discardRectangleMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBeginDebugUtilsLabelEXT>(*device,"vkCmdBeginDebugUtilsLabelEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pLabelInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdEndDebugUtilsLabelEXT>(*device,"vkCmdEndDebugUtilsLabelEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdInsertDebugUtilsLabelEXT>(*device,"vkCmdInsertDebugUtilsLabelEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pLabelInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer, const VkSampleLocationsInfoEXT* pSampleLocationsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetSampleLocationsEXT>(*device,"vkCmdSetSampleLocationsEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pSampleLocationsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindShadingRateImageNV(VkCommandBuffer commandBuffer, VkImageView imageView, VkImageLayout imageLayout){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindShadingRateImageNV>(*device,"vkCmdBindShadingRateImageNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, imageView, imageLayout);});
    function(actual, imageView, imageLayout);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewportShadingRatePaletteNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkShadingRatePaletteNV* pShadingRatePalettes){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetViewportShadingRatePaletteNV>(*device,"vkCmdSetViewportShadingRatePaletteNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstViewport, viewportCount, pShadingRatePalettes);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCoarseSampleOrderNV(VkCommandBuffer commandBuffer, VkCoarseSampleOrderTypeNV sampleOrderType, uint32_t customSampleOrderCount, const VkCoarseSampleOrderCustomNV* pCustomSampleOrders){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCoarseSampleOrderNV>(*device,"vkCmdSetCoarseSampleOrderNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, sampleOrderType, customSampleOrderCount, pCustomSampleOrders);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBuildAccelerationStructureNV(VkCommandBuffer commandBuffer, const VkAccelerationStructureInfoNV* pInfo, VkBuffer instanceData, VkDeviceSize instanceOffset, VkBool32 update, VkAccelerationStructureNV dst, VkAccelerationStructureNV src, VkBuffer scratch, VkDeviceSize scratchOffset){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBuildAccelerationStructureNV>(*device,"vkCmdBuildAccelerationStructureNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pInfo, instanceData, instanceOffset, update, dst, src, scratch, scratchOffset);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyAccelerationStructureNV(VkCommandBuffer commandBuffer, VkAccelerationStructureNV dst, VkAccelerationStructureNV src, VkCopyAccelerationStructureModeKHR mode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyAccelerationStructureNV>(*device,"vkCmdCopyAccelerationStructureNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, dst, src, mode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdTraceRaysNV(VkCommandBuffer commandBuffer, VkBuffer raygenShaderBindingTableBuffer, VkDeviceSize raygenShaderBindingOffset, VkBuffer missShaderBindingTableBuffer, VkDeviceSize missShaderBindingOffset, VkDeviceSize missShaderBindingStride, VkBuffer hitShaderBindingTableBuffer, VkDeviceSize hitShaderBindingOffset, VkDeviceSize hitShaderBindingStride, VkBuffer callableShaderBindingTableBuffer, VkDeviceSize callableShaderBindingOffset, VkDeviceSize callableShaderBindingStride, uint32_t width, uint32_t height, uint32_t depth){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdTraceRaysNV>(*device,"vkCmdTraceRaysNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, raygenShaderBindingTableBuffer, raygenShaderBindingOffset, missShaderBindingTableBuffer, missShaderBindingOffset, missShaderBindingStride, hitShaderBindingTableBuffer, hitShaderBindingOffset, hitShaderBindingStride, callableShaderBindingTableBuffer, callableShaderBindingOffset, callableShaderBindingStride, width, height, depth);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteAccelerationStructuresPropertiesNV(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureNV* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteAccelerationStructuresPropertiesNV>(*device,"vkCmdWriteAccelerationStructuresPropertiesNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteBufferMarkerAMD(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteBufferMarkerAMD>(*device,"vkCmdWriteBufferMarkerAMD"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pipelineStage, dstBuffer, dstOffset, marker);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkBuffer dstBuffer, VkDeviceSize dstOffset, uint32_t marker){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteBufferMarker2AMD>(*device,"vkCmdWriteBufferMarker2AMD"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, stage, dstBuffer, dstOffset, marker);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMeshTasksNV(VkCommandBuffer commandBuffer, uint32_t taskCount, uint32_t firstTask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMeshTasksNV>(*device,"vkCmdDrawMeshTasksNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, taskCount, firstTask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMeshTasksIndirectNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMeshTasksIndirectNV>(*device,"vkCmdDrawMeshTasksIndirectNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMeshTasksIndirectCountNV(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMeshTasksIndirectCountNV>(*device,"vkCmdDrawMeshTasksIndirectCountNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetExclusiveScissorEnableNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkBool32* pExclusiveScissorEnables){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetExclusiveScissorEnableNV>(*device,"vkCmdSetExclusiveScissorEnableNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstExclusiveScissor, exclusiveScissorCount, pExclusiveScissorEnables);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetExclusiveScissorNV(VkCommandBuffer commandBuffer, uint32_t firstExclusiveScissor, uint32_t exclusiveScissorCount, const VkRect2D* pExclusiveScissors){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetExclusiveScissorNV>(*device,"vkCmdSetExclusiveScissorNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstExclusiveScissor, exclusiveScissorCount, pExclusiveScissors);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCheckpointNV(VkCommandBuffer commandBuffer, const void* pCheckpointMarker){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCheckpointNV>(*device,"vkCmdSetCheckpointNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pCheckpointMarker);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLineStippleEXT(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLineStippleEXT>(*device,"vkCmdSetLineStippleEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, lineStippleFactor, lineStipplePattern);});
    function(actual, lineStippleFactor, lineStipplePattern);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCullModeEXT(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCullModeEXT>(*device,"vkCmdSetCullModeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetCullMode(commandBuffer,cullMode);
    function(actual, cullMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetFrontFaceEXT(VkCommandBuffer commandBuffer, VkFrontFace frontFace){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetFrontFaceEXT>(*device,"vkCmdSetFrontFaceEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetFrontFace(commandBuffer,frontFace);
    function(actual, frontFace);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetPrimitiveTopologyEXT(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetPrimitiveTopologyEXT>(*device,"vkCmdSetPrimitiveTopologyEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetPrimitiveTopology(commandBuffer,primitiveTopology);
    function(actual, primitiveTopology);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewportWithCountEXT(VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport* pViewports){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetViewportWithCountEXT>(*device,"vkCmdSetViewportWithCountEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_viewport_count(*device,commandBuffer,function,viewportCount,pViewports);
    function(actual, viewportCount, pViewports);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetScissorWithCountEXT(VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D* pScissors){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetScissorWithCountEXT>(*device,"vkCmdSetScissorWithCountEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_scissor_count(*device,commandBuffer,function,scissorCount,pScissors);
    function(actual, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindVertexBuffers2EXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, const VkDeviceSize* pSizes, const VkDeviceSize* pStrides){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindVertexBuffers2EXT>(*device,"vkCmdBindVertexBuffers2EXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_vertex2(*device,commandBuffer,function,firstBinding,bindingCount,pBuffers,pOffsets,pSizes,pStrides);
    function(actual, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthTestEnableEXT>(*device,"vkCmdSetDepthTestEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthTestEnable(commandBuffer,depthTestEnable);
    function(actual, depthTestEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthWriteEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthWriteEnableEXT>(*device,"vkCmdSetDepthWriteEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthWriteEnable(commandBuffer,depthWriteEnable);
    function(actual, depthWriteEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthCompareOpEXT(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthCompareOpEXT>(*device,"vkCmdSetDepthCompareOpEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthCompareOp(commandBuffer,depthCompareOp);
    function(actual, depthCompareOp);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBoundsTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthBoundsTestEnableEXT>(*device,"vkCmdSetDepthBoundsTestEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetDepthBoundsTestEnable(commandBuffer,depthBoundsTestEnable);
    function(actual, depthBoundsTestEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilTestEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetStencilTestEnableEXT>(*device,"vkCmdSetStencilTestEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetStencilTestEnable(commandBuffer,stencilTestEnable);
    function(actual, stencilTestEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilOpEXT(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetStencilOpEXT>(*device,"vkCmdSetStencilOpEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->tracker.OnSetStencilOp(commandBuffer,faceMask,failOp,passOp,depthFailOp,compareOp);
    function(actual, faceMask, failOp, passOp, depthFailOp, compareOp);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPreprocessGeneratedCommandsNV(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPreprocessGeneratedCommandsNV>(*device,"vkCmdPreprocessGeneratedCommandsNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pGeneratedCommandsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdExecuteGeneratedCommandsNV(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoNV* pGeneratedCommandsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdExecuteGeneratedCommandsNV>(*device,"vkCmdExecuteGeneratedCommandsNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, isPreprocessed, pGeneratedCommandsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindPipelineShaderGroupNV(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline, uint32_t groupIndex){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindPipelineShaderGroupNV>(*device,"vkCmdBindPipelineShaderGroupNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, pipelineBindPoint, pipeline, groupIndex);});
    function(actual, pipelineBindPoint, pipeline, groupIndex);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBias2EXT(VkCommandBuffer commandBuffer, const VkDepthBiasInfoEXT* pDepthBiasInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthBias2EXT>(*device,"vkCmdSetDepthBias2EXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pDepthBiasInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCudaLaunchKernelNV(VkCommandBuffer commandBuffer, const VkCudaLaunchInfoNV* pLaunchInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCudaLaunchKernelNV>(*device,"vkCmdCudaLaunchKernelNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pLaunchInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindDescriptorBuffersEXT(VkCommandBuffer commandBuffer, uint32_t bufferCount, const VkDescriptorBufferBindingInfoEXT* pBindingInfos){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindDescriptorBuffersEXT>(*device,"vkCmdBindDescriptorBuffersEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, bufferCount, pBindingInfos);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t setCount, const uint32_t* pBufferIndices, const VkDeviceSize* pOffsets){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(*device,"vkCmdSetDescriptorBufferOffsetsEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, pipelineBindPoint, layout, firstSet, setCount, pBufferIndices, pOffsets);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT>(*device,"vkCmdBindDescriptorBufferEmbeddedSamplersEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, pipelineBindPoint, layout, set);});
    function(actual, pipelineBindPoint, layout, set);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetFragmentShadingRateEnumNV(VkCommandBuffer commandBuffer, VkFragmentShadingRateNV shadingRate, const VkFragmentShadingRateCombinerOpKHR combinerOps[2]){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetFragmentShadingRateEnumNV>(*device,"vkCmdSetFragmentShadingRateEnumNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, shadingRate, combinerOps);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetVertexInputEXT(VkCommandBuffer commandBuffer, uint32_t vertexBindingDescriptionCount, const VkVertexInputBindingDescription2EXT* pVertexBindingDescriptions, uint32_t vertexAttributeDescriptionCount, const VkVertexInputAttributeDescription2EXT* pVertexAttributeDescriptions){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetVertexInputEXT>(*device,"vkCmdSetVertexInputEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, vertexBindingDescriptionCount, pVertexBindingDescriptions, vertexAttributeDescriptionCount, pVertexAttributeDescriptions);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSubpassShadingHUAWEI(VkCommandBuffer commandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSubpassShadingHUAWEI>(*device,"vkCmdSubpassShadingHUAWEI"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindInvocationMaskHUAWEI(VkCommandBuffer commandBuffer, VkImageView imageView, VkImageLayout imageLayout){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindInvocationMaskHUAWEI>(*device,"vkCmdBindInvocationMaskHUAWEI"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, imageView, imageLayout);});
    function(actual, imageView, imageLayout);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetPatchControlPointsEXT(VkCommandBuffer commandBuffer, uint32_t patchControlPoints){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetPatchControlPointsEXT>(*device,"vkCmdSetPatchControlPointsEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, patchControlPoints);});
    function(actual, patchControlPoints);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRasterizerDiscardEnableEXT(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRasterizerDiscardEnableEXT>(*device,"vkCmdSetRasterizerDiscardEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, rasterizerDiscardEnable);});
    function(actual, rasterizerDiscardEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBiasEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthBiasEnableEXT>(*device,"vkCmdSetDepthBiasEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, depthBiasEnable);});
    function(actual, depthBiasEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLogicOpEXT(VkCommandBuffer commandBuffer, VkLogicOp logicOp){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLogicOpEXT>(*device,"vkCmdSetLogicOpEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, logicOp);});
    function(actual, logicOp);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetPrimitiveRestartEnableEXT(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetPrimitiveRestartEnableEXT>(*device,"vkCmdSetPrimitiveRestartEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, primitiveRestartEnable);});
    function(actual, primitiveRestartEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetColorWriteEnableEXT(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkBool32* pColorWriteEnables){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetColorWriteEnableEXT>(*device,"vkCmdSetColorWriteEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, attachmentCount, pColorWriteEnables);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMultiEXT(VkCommandBuffer commandBuffer, uint32_t drawCount, const VkMultiDrawInfoEXT* pVertexInfo, uint32_t instanceCount, uint32_t firstInstance, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMultiEXT>(*device,"vkCmdDrawMultiEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, drawCount, pVertexInfo, instanceCount, firstInstance, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer, uint32_t drawCount, const VkMultiDrawIndexedInfoEXT* pIndexInfo, uint32_t instanceCount, uint32_t firstInstance, uint32_t stride, const int32_t* pVertexOffset){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMultiIndexedEXT>(*device,"vkCmdDrawMultiIndexedEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, drawCount, pIndexInfo, instanceCount, firstInstance, stride, pVertexOffset);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBuildMicromapsEXT(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkMicromapBuildInfoEXT* pInfos){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBuildMicromapsEXT>(*device,"vkCmdBuildMicromapsEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, infoCount, pInfos);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapInfoEXT* pInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyMicromapEXT>(*device,"vkCmdCopyMicromapEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer commandBuffer, const VkCopyMicromapToMemoryInfoEXT* pInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyMicromapToMemoryEXT>(*device,"vkCmdCopyMicromapToMemoryEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer commandBuffer, const VkCopyMemoryToMicromapInfoEXT* pInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyMemoryToMicromapEXT>(*device,"vkCmdCopyMemoryToMicromapEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer commandBuffer, uint32_t micromapCount, const VkMicromapEXT* pMicromaps, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteMicromapsPropertiesEXT>(*device,"vkCmdWriteMicromapsPropertiesEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, micromapCount, pMicromaps, queryType, queryPool, firstQuery);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawClusterHUAWEI(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawClusterHUAWEI>(*device,"vkCmdDrawClusterHUAWEI"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawClusterIndirectHUAWEI(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawClusterIndirectHUAWEI>(*device,"vkCmdDrawClusterIndirectHUAWEI"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyMemoryIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress, uint32_t copyCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyMemoryIndirectNV>(*device,"vkCmdCopyMemoryIndirectNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, copyBufferAddress, copyCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyMemoryToImageIndirectNV(VkCommandBuffer commandBuffer, VkDeviceAddress copyBufferAddress, uint32_t copyCount, uint32_t stride, VkImage dstImage, VkImageLayout dstImageLayout, const VkImageSubresourceLayers* pImageSubresources){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyMemoryToImageIndirectNV>(*device,"vkCmdCopyMemoryToImageIndirectNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, copyBufferAddress, copyCount, stride, dstImage, dstImageLayout, pImageSubresources);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDecompressMemoryNV(VkCommandBuffer commandBuffer, uint32_t decompressRegionCount, const VkDecompressMemoryRegionNV* pDecompressMemoryRegions){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDecompressMemoryNV>(*device,"vkCmdDecompressMemoryNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, decompressRegionCount, pDecompressMemoryRegions);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDecompressMemoryIndirectCountNV(VkCommandBuffer commandBuffer, VkDeviceAddress indirectCommandsAddress, VkDeviceAddress indirectCommandsCountAddress, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDecompressMemoryIndirectCountNV>(*device,"vkCmdDecompressMemoryIndirectCountNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, indirectCommandsAddress, indirectCommandsCountAddress, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdUpdatePipelineIndirectBufferNV(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdUpdatePipelineIndirectBufferNV>(*device,"vkCmdUpdatePipelineIndirectBufferNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pipelineBindPoint, pipeline);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthClampEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClampEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthClampEnableEXT>(*device,"vkCmdSetDepthClampEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, depthClampEnable);});
    function(actual, depthClampEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetPolygonModeEXT(VkCommandBuffer commandBuffer, VkPolygonMode polygonMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetPolygonModeEXT>(*device,"vkCmdSetPolygonModeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, polygonMode);});
    function(actual, polygonMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRasterizationSamplesEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits rasterizationSamples){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRasterizationSamplesEXT>(*device,"vkCmdSetRasterizationSamplesEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, rasterizationSamples);});
    function(actual, rasterizationSamples);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetSampleMaskEXT(VkCommandBuffer commandBuffer, VkSampleCountFlagBits samples, const VkSampleMask* pSampleMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetSampleMaskEXT>(*device,"vkCmdSetSampleMaskEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, samples, pSampleMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetAlphaToCoverageEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToCoverageEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetAlphaToCoverageEnableEXT>(*device,"vkCmdSetAlphaToCoverageEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, alphaToCoverageEnable);});
    function(actual, alphaToCoverageEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetAlphaToOneEnableEXT(VkCommandBuffer commandBuffer, VkBool32 alphaToOneEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetAlphaToOneEnableEXT>(*device,"vkCmdSetAlphaToOneEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, alphaToOneEnable);});
    function(actual, alphaToOneEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLogicOpEnableEXT(VkCommandBuffer commandBuffer, VkBool32 logicOpEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLogicOpEnableEXT>(*device,"vkCmdSetLogicOpEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, logicOpEnable);});
    function(actual, logicOpEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetColorBlendEnableEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkBool32* pColorBlendEnables){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetColorBlendEnableEXT>(*device,"vkCmdSetColorBlendEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstAttachment, attachmentCount, pColorBlendEnables);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetColorBlendEquationEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorBlendEquationEXT* pColorBlendEquations){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetColorBlendEquationEXT>(*device,"vkCmdSetColorBlendEquationEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstAttachment, attachmentCount, pColorBlendEquations);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetColorWriteMaskEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorComponentFlags* pColorWriteMasks){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetColorWriteMaskEXT>(*device,"vkCmdSetColorWriteMaskEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstAttachment, attachmentCount, pColorWriteMasks);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetTessellationDomainOriginEXT(VkCommandBuffer commandBuffer, VkTessellationDomainOrigin domainOrigin){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetTessellationDomainOriginEXT>(*device,"vkCmdSetTessellationDomainOriginEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, domainOrigin);});
    function(actual, domainOrigin);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRasterizationStreamEXT(VkCommandBuffer commandBuffer, uint32_t rasterizationStream){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRasterizationStreamEXT>(*device,"vkCmdSetRasterizationStreamEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, rasterizationStream);});
    function(actual, rasterizationStream);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetConservativeRasterizationModeEXT(VkCommandBuffer commandBuffer, VkConservativeRasterizationModeEXT conservativeRasterizationMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetConservativeRasterizationModeEXT>(*device,"vkCmdSetConservativeRasterizationModeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, conservativeRasterizationMode);});
    function(actual, conservativeRasterizationMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetExtraPrimitiveOverestimationSizeEXT(VkCommandBuffer commandBuffer, float extraPrimitiveOverestimationSize){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetExtraPrimitiveOverestimationSizeEXT>(*device,"vkCmdSetExtraPrimitiveOverestimationSizeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, extraPrimitiveOverestimationSize);});
    function(actual, extraPrimitiveOverestimationSize);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthClipEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClipEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthClipEnableEXT>(*device,"vkCmdSetDepthClipEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, depthClipEnable);});
    function(actual, depthClipEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetSampleLocationsEnableEXT(VkCommandBuffer commandBuffer, VkBool32 sampleLocationsEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetSampleLocationsEnableEXT>(*device,"vkCmdSetSampleLocationsEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, sampleLocationsEnable);});
    function(actual, sampleLocationsEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetColorBlendAdvancedEXT(VkCommandBuffer commandBuffer, uint32_t firstAttachment, uint32_t attachmentCount, const VkColorBlendAdvancedEXT* pColorBlendAdvanced){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetColorBlendAdvancedEXT>(*device,"vkCmdSetColorBlendAdvancedEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstAttachment, attachmentCount, pColorBlendAdvanced);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetProvokingVertexModeEXT(VkCommandBuffer commandBuffer, VkProvokingVertexModeEXT provokingVertexMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetProvokingVertexModeEXT>(*device,"vkCmdSetProvokingVertexModeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, provokingVertexMode);});
    function(actual, provokingVertexMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLineRasterizationModeEXT(VkCommandBuffer commandBuffer, VkLineRasterizationModeEXT lineRasterizationMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLineRasterizationModeEXT>(*device,"vkCmdSetLineRasterizationModeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, lineRasterizationMode);});
    function(actual, lineRasterizationMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLineStippleEnableEXT(VkCommandBuffer commandBuffer, VkBool32 stippledLineEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetLineStippleEnableEXT>(*device,"vkCmdSetLineStippleEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, stippledLineEnable);});
    function(actual, stippledLineEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthClipNegativeOneToOneEXT(VkCommandBuffer commandBuffer, VkBool32 negativeOneToOne){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthClipNegativeOneToOneEXT>(*device,"vkCmdSetDepthClipNegativeOneToOneEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, negativeOneToOne);});
    function(actual, negativeOneToOne);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewportWScalingEnableNV(VkCommandBuffer commandBuffer, VkBool32 viewportWScalingEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetViewportWScalingEnableNV>(*device,"vkCmdSetViewportWScalingEnableNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, viewportWScalingEnable);});
    function(actual, viewportWScalingEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewportSwizzleNV(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewportSwizzleNV* pViewportSwizzles){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetViewportSwizzleNV>(*device,"vkCmdSetViewportSwizzleNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, firstViewport, viewportCount, pViewportSwizzles);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCoverageToColorEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageToColorEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCoverageToColorEnableNV>(*device,"vkCmdSetCoverageToColorEnableNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, coverageToColorEnable);});
    function(actual, coverageToColorEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCoverageToColorLocationNV(VkCommandBuffer commandBuffer, uint32_t coverageToColorLocation){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCoverageToColorLocationNV>(*device,"vkCmdSetCoverageToColorLocationNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, coverageToColorLocation);});
    function(actual, coverageToColorLocation);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCoverageModulationModeNV(VkCommandBuffer commandBuffer, VkCoverageModulationModeNV coverageModulationMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCoverageModulationModeNV>(*device,"vkCmdSetCoverageModulationModeNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, coverageModulationMode);});
    function(actual, coverageModulationMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCoverageModulationTableEnableNV(VkCommandBuffer commandBuffer, VkBool32 coverageModulationTableEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCoverageModulationTableEnableNV>(*device,"vkCmdSetCoverageModulationTableEnableNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, coverageModulationTableEnable);});
    function(actual, coverageModulationTableEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCoverageModulationTableNV(VkCommandBuffer commandBuffer, uint32_t coverageModulationTableCount, const float* pCoverageModulationTable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCoverageModulationTableNV>(*device,"vkCmdSetCoverageModulationTableNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, coverageModulationTableCount, pCoverageModulationTable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetShadingRateImageEnableNV(VkCommandBuffer commandBuffer, VkBool32 shadingRateImageEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetShadingRateImageEnableNV>(*device,"vkCmdSetShadingRateImageEnableNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, shadingRateImageEnable);});
    function(actual, shadingRateImageEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRepresentativeFragmentTestEnableNV(VkCommandBuffer commandBuffer, VkBool32 representativeFragmentTestEnable){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRepresentativeFragmentTestEnableNV>(*device,"vkCmdSetRepresentativeFragmentTestEnableNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, representativeFragmentTestEnable);});
    function(actual, representativeFragmentTestEnable);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetCoverageReductionModeNV(VkCommandBuffer commandBuffer, VkCoverageReductionModeNV coverageReductionMode){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetCoverageReductionModeNV>(*device,"vkCmdSetCoverageReductionModeNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, coverageReductionMode);});
    function(actual, coverageReductionMode);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdOpticalFlowExecuteNV(VkCommandBuffer commandBuffer, VkOpticalFlowSessionNV session, const VkOpticalFlowExecuteInfoNV* pExecuteInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdOpticalFlowExecuteNV>(*device,"vkCmdOpticalFlowExecuteNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, session, pExecuteInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindShadersEXT(VkCommandBuffer commandBuffer, uint32_t stageCount, const VkShaderStageFlagBits* pStages, const VkShaderEXT* pShaders){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBindShadersEXT>(*device,"vkCmdBindShadersEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, stageCount, pStages, pShaders);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthClampRangeEXT(VkCommandBuffer commandBuffer, VkDepthClampModeEXT depthClampMode, const VkDepthClampRangeEXT* pDepthClampRange){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetDepthClampRangeEXT>(*device,"vkCmdSetDepthClampRangeEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    device->commands[commandBuffer].unsupported_state=true;
    function(actual, depthClampMode, pDepthClampRange);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdConvertCooperativeVectorMatrixNV(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkConvertCooperativeVectorMatrixInfoNV* pInfos){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdConvertCooperativeVectorMatrixNV>(*device,"vkCmdConvertCooperativeVectorMatrixNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, infoCount, pInfos);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetAttachmentFeedbackLoopEnableEXT(VkCommandBuffer commandBuffer, VkImageAspectFlags aspectMask){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetAttachmentFeedbackLoopEnableEXT>(*device,"vkCmdSetAttachmentFeedbackLoopEnableEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, aspectMask);});
    function(actual, aspectMask);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBuildClusterAccelerationStructureIndirectNV(VkCommandBuffer commandBuffer, const VkClusterAccelerationStructureCommandsInfoNV* pCommandInfos){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBuildClusterAccelerationStructureIndirectNV>(*device,"vkCmdBuildClusterAccelerationStructureIndirectNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pCommandInfos);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBuildPartitionedAccelerationStructuresNV(VkCommandBuffer commandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBuildPartitionedAccelerationStructuresNV>(*device,"vkCmdBuildPartitionedAccelerationStructuresNV"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pBuildInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo, VkCommandBuffer stateCommandBuffer){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdPreprocessGeneratedCommandsEXT>(*device,"vkCmdPreprocessGeneratedCommandsEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pGeneratedCommandsInfo, stateCommandBuffer);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdExecuteGeneratedCommandsEXT>(*device,"vkCmdExecuteGeneratedCommandsEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, isPreprocessed, pGeneratedCommandsInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBuildAccelerationStructuresKHR>(*device,"vkCmdBuildAccelerationStructuresKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, infoCount, pInfos, ppBuildRangeInfos);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkDeviceAddress* pIndirectDeviceAddresses, const uint32_t* pIndirectStrides, const uint32_t* const* ppMaxPrimitiveCounts){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdBuildAccelerationStructuresIndirectKHR>(*device,"vkCmdBuildAccelerationStructuresIndirectKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, infoCount, pInfos, pIndirectDeviceAddresses, pIndirectStrides, ppMaxPrimitiveCounts);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyAccelerationStructureKHR>(*device,"vkCmdCopyAccelerationStructureKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyAccelerationStructureToMemoryKHR>(*device,"vkCmdCopyAccelerationStructureToMemoryKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdCopyMemoryToAccelerationStructureKHR>(*device,"vkCmdCopyMemoryToAccelerationStructureKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pInfo);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(*device,"vkCmdWriteAccelerationStructuresPropertiesKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdTraceRaysKHR>(*device,"vkCmdTraceRaysKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdTraceRaysIndirectKHR>(*device,"vkCmdTraceRaysIndirectKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, indirectDeviceAddress);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdSetRayTracingPipelineStackSizeKHR>(*device,"vkCmdSetRayTracingPipelineStackSizeKHR"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    remember_state(*device,commandBuffer,[=](VkCommandBuffer target){function(target, pipelineStackSize);});
    function(actual, pipelineStackSize);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMeshTasksEXT(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMeshTasksEXT>(*device,"vkCmdDrawMeshTasksEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMeshTasksIndirectEXT>(*device,"vkCmdDrawMeshTasksIndirectEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdDrawMeshTasksIndirectCountEXT>(*device,"vkCmdDrawMeshTasksIndirectCountEXT"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL hook_vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers){
    auto device=command_device(commandBuffer); if(!device)return;
    auto function=load<PFN_vkCmdExecuteCommands>(*device,"vkCmdExecuteCommands"); if(!function)return;
    std::scoped_lock lock(device->mutex);
    auto actual=native_locked(*device,commandBuffer);
    function(actual, commandBufferCount, pCommandBuffers);
}
