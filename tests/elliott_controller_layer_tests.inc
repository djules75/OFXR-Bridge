// Actual production hooks, not a substitute action merger.
void elliott_controller_layer_test(){
    const auto original=xrfg::autonomous::snapshot().desired;
    auto desired=original;desired.overlay_position=5;check(xrfg::autonomous::request(desired),"controller setting");
    auto s=session();s->dispatch->instance=reinterpret_cast<XrInstance>(0x471);
    auto& g=s->dispatch->panel_gesture;
    g.action_set=reinterpret_cast<XrActionSet>(0x4710);g.grip_action=reinterpret_cast<XrAction>(0x4711);
    g.grips={11,12};g.hands={21,22};
    static bool refuse;static unsigned calls,count,spaces;
    static const void* next;static const void* expected_next;
    g.suggest_bindings=+[](XrInstance,const XrInteractionProfileSuggestedBinding* i)->XrResult{
        ++calls;count=i->countSuggestedBindings;next=i->next;
        return refuse&&count==3?XR_ERROR_PATH_UNSUPPORTED:XR_SUCCESS;};
    g.attach_action_sets=+[](XrSession,const XrSessionActionSetsAttachInfo* i)->XrResult{
        ++calls;count=i->countActionSets;next=i->next;
        return refuse&&count==2?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;};
    g.sync_actions=+[](XrSession,const XrActionsSyncInfo* i)->XrResult{
        ++calls;count=i->countActiveActionSets;next=i->next;
        return refuse&&count==2?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;};
    g.create_action_space=+[](XrSession,const XrActionSpaceCreateInfo* i,XrSpace* out)->XrResult{
        check(i->poseInActionSpace.orientation.w==1,"identity grip offset");
        ++spaces;*out=reinterpret_cast<XrSpace>(std::uintptr_t(0x4712+spaces));return XR_SUCCESS;};
    {std::scoped_lock l(g_state_mutex);g_instances[s->dispatch->instance]=s->dispatch;g_sessions[s->handle]=s;}
    struct Cleanup{std::shared_ptr<SessionState>s;xrfg::autonomous::Settings original;
        ~Cleanup(){xrfg::autonomous::request(original);std::scoped_lock l(g_state_mutex);g_instances.erase(s->dispatch->instance);g_sessions.erase(s->handle);}}clean{s,original};
    int tail{};expected_next=&tail;
    XrActionSuggestedBinding binding{reinterpret_cast<XrAction>(0x100),99};
    XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestion.next=&tail;suggestion.countSuggestedBindings=1;suggestion.suggestedBindings=&binding;
    calls=0;refuse=false;
    check(layer_suggest_interaction_profile_bindings(s->dispatch->instance,&suggestion)==XR_SUCCESS&&calls==1&&count==3&&next==expected_next,"grip bindings appended, next preserved");
    calls=0;refuse=true;
    check(layer_suggest_interaction_profile_bindings(s->dispatch->instance,&suggestion)==XR_SUCCESS&&calls==2&&count==1,"rejected profile falls back to original");
    XrActionSet app=reinterpret_cast<XrActionSet>(0x100);
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.next=&tail;attach.countActionSets=1;attach.actionSets=&app;
    calls=spaces=0;
    check(layer_attach_session_action_sets(s->handle,&attach)==XR_SUCCESS&&calls==2&&count==1&&!s->panel_actions_attached&&spaces==0,"refused attach preserves app and disables gesture");
    calls=0;refuse=false;
    check(layer_attach_session_action_sets(s->handle,&attach)==XR_SUCCESS&&calls==1&&count==2&&spaces==2&&s->panel_actions_attached,"grip spaces made after accepted attach");
    XrActiveActionSet active{app,0};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.next=&tail;sync.countActiveActionSets=1;sync.activeActionSets=&active;
    calls=0;check(layer_sync_actions(s->handle,&sync)==XR_SUCCESS&&count==2&&next==expected_next,"follow sync adds pose set");
    calls=0;refuse=true;check(layer_sync_actions(s->handle,&sync)==XR_SUCCESS&&calls==2&&count==1,"refused sync preserves app");
    desired.overlay_position=6;check(xrfg::autonomous::request(desired),"bottom setting");calls=0;
    check(layer_sync_actions(s->handle,&sync)==XR_SUCCESS&&calls==1&&count==1,"bottom mode performs no extra pose sync");
    std::cout<<"Controller production hooks preserve app inputs and refusal fallback\n";
}
