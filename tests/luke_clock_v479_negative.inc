XrResult XRAPI_CALL legacy_content_end_frame_v479(XrSession session,const XrFrameEndInfo* info) {
    const auto state=find_session(session);
    if(!state || !state->dispatch->end_frame)return XR_ERROR_HANDLE_INVALID;
    if(!state->luke_fresh_only)return state->dispatch->end_frame(session,info);
    if(!info || info->type!=XR_TYPE_FRAME_END_INFO)return XR_ERROR_VALIDATION_FAILURE;
    if(!state->luke_output_begun) {
        XrFrameState slot{XR_TYPE_FRAME_STATE};
        if(state->runtime_frame_waited_unbegun && state->last_inline_frame_state_valid)
            slot=state->last_inline_frame_state;
        else {
            XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
            const auto result=state->dispatch->wait_frame(session,&wait,&slot);
            if(XR_FAILED(result))return result;
            state->runtime_frame_waited_unbegun=true;
            state->last_inline_frame_state=slot;
            state->last_inline_frame_state_valid=true;
        }
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        const auto result=state->dispatch->begin_frame(session,&begin);
        if(XR_FAILED(result))return result;
        state->runtime_frame_waited_unbegun=false;
        state->luke_output_begun=true;
        state->luke_output_slot=slot;
        state->last_inline_frame_state=slot;
        state->last_inline_frame_state_valid=true;
        state->luke_prediction_origin=slot.predictedDisplayTime;
        state->luke_prediction_wall=std::chrono::steady_clock::now();
    }
    auto submitted=*info;
    // Pose/FOV/space/rect are the original immutable image metadata. Only the
    // actual physical presentation slot is assigned here.
    submitted.displayTime=state->luke_output_slot.predictedDisplayTime;
    if(state->luke_output_slot.shouldRender==XR_FALSE) {
        submitted.layerCount=0;
        submitted.layers=nullptr;
    }
    const auto result=state->dispatch->end_frame(session,&submitted);
    state->luke_output_begun=false;
    return result;
}
