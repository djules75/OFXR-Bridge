// legacy_content_end_frame_v479 below is extracted unchanged (name excepted)
// from sealed e14f9050392f0def179e4028d39f872c0e4ed6dc. Test-only negative.
#include "luke_clock_v479_negative.inc"
void luke_clock_continuity_test() {
    auto probe=[](bool legacy) {
        auto s=session();s->luke_fresh_only=true;s->luke_afw_enabled=true;
        s->presenter_active=false;s->three_slot_director=false;
        s->last_inline_frame_state_valid=true;
        s->last_inline_frame_state={XR_TYPE_FRAME_STATE};
        s->last_inline_frame_state.predictedDisplayPeriod=2'000'000;
        s->last_inline_frame_state.predictedDisplayTime=1'100'000'000;
        s->last_inline_frame_state.shouldRender=XR_TRUE;
        s->luke_prediction_origin=1'100'000'000;
        s->last_virtual_display_time=1'100'000'000;
        const auto now=std::chrono::steady_clock::now();
        s->luke_prediction_wall=now-std::chrono::milliseconds(21);
        s->application_wait_returned_at=s->luke_prediction_wall;
        next_time=1'000'000'000; // runtime slot trails producer after GPU work
        waits=begins=ends=0;
        {std::scoped_lock lock(g_state_mutex);g_sessions[s->handle]=s;}
        const auto output=frame(1);
        check(XR_SUCCEEDED(legacy ? legacy_content_end_frame_v479(s->handle,&output->frame->info) :
            content_end_frame(s->handle,&output->frame->info)),"clock physical output");
        check(waits==1 && begins==1 && ends==1,"clock physical cycle changed");
        XrFrameState prediction{XR_TYPE_FRAME_STATE};
        check(XR_SUCCEEDED(layer_wait_frame_impl(s->handle,nullptr,&prediction)),"post-GPU virtual wait");
        const auto delta=prediction.predictedDisplayTime-1'100'000'000;
        check(waits==1,"clock fix added runtime polling");
        check(XR_SUCCEEDED(layer_begin_frame_impl(s->handle,nullptr)),"clock virtual begin");
        consume_application_frame(s,prediction.predictedDisplayTime,false,true);
        {std::scoped_lock lock(g_state_mutex);g_sessions.erase(s->handle);}
        return delta;
    };
    const auto old_delta=probe(true),new_delta=probe(false);
    check(old_delta==1,"sealed V479 negative did not reproduce frozen prediction");
    check(new_delta>=21'000'000 && new_delta<50'000'000,"elapsed producer clock lost or advanced too far");
    auto s=session();
    s->luke_prediction_origin=1'000'000'000;
    s->luke_prediction_wall=std::chrono::steady_clock::now();
    s->last_virtual_display_time=1'000'000'000;
    XrFrameState newer{XR_TYPE_FRAME_STATE};newer.predictedDisplayTime=1'030'000'000;
    refresh_luke_prediction_clock(*s,newer,s->luke_prediction_wall);
    check(s->luke_prediction_origin==newer.predictedDisplayTime,"newer physical anchor rejected");
    auto t=s->luke_prediction_wall;
    newer.predictedDisplayTime=1'010'000'000;
    refresh_luke_prediction_clock(*s,newer,t+std::chrono::milliseconds(7));
    check(s->luke_prediction_origin==1'037'000'000,"second backward anchor reset clock");
    std::cout<<"Actual output/Wait clock: V479 negative delta="<<old_delta
        <<"ns; fixed delta="<<new_delta<<"ns; physical cycles unchanged; newer/stale anchors verified\n";
}
