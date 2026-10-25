#include "xrfg/presented_output_rate.hpp"
#include "xrfg/compositor_presentation_rate.hpp"
#include "xrfg/scene_presentation_rate.hpp"
#include "xrfg/accepted_scene_rate.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Timing {
    unsigned m_nFrameIndex{}, m_nNumFramePresents{}, m_nNumMisPresented{};
    double m_flSystemTimeInSeconds{};
};
void check(const std::vector<Timing>& records, float expected) {
    const auto rate = xrfg::compositor_presentation_rate(std::span<const Timing>(records));
    if (!rate || std::abs(rate->fps - expected) > 0.01F)
        throw std::runtime_error("incorrect compositor presentation rate");
}
int main() {
    try {
        {
            // All90 compositor submissions are shown, but only25real+25gen
            // identities are new; forty held re-submissions are not new FPS.
            xrfg::ScenePresentationRate scenes;
            std::vector<Timing> records;
            xrfg::SceneIdentity identity{1,1,0};
            for(unsigned i=0;i<=90;++i) {
                if(i&&i<=50)identity={1,2+(i-1)/2,i%2};
                scenes.record(i,identity);records.push_back({i,1,0,i/90.0});
            }
            auto rate=scenes.rate(std::span<const Timing>(records));
            if(!rate||std::abs(rate->output-50)>.01||rate->real!=25||rate->generated!=25)
                throw std::runtime_error("scene repeats became extra FPS");
            xrfg::ScenePresentationRate raw;unsigned serial=1;
            for(unsigned i=0;i<=90;++i) {
                if(i&&i*47/90>(i-1)*47/90)++serial;
                raw.record(i,{2,serial,0});
            }
            rate=raw.rate(std::span<const Timing>(records));
            if(!rate||rate->output!=47||rate->real!=47||rate->generated!=0)
                throw std::runtime_error("47 source frames became90XR calls");
            scenes.record(30,{99,99,1});
            if(scenes.rate(std::span<const Timing>(records)))
                throw std::runtime_error("ambiguous compositor attribution was accepted");
            xrfg::ScenePresentationRate unknown;
            for(unsigned i=0;i<=90;++i)unknown.record(i,i==40?xrfg::SceneIdentity{}:xrfg::SceneIdentity{1,i+1,0});
            if(unknown.rate(std::span<const Timing>(records)))
                throw std::runtime_error("unknown source identity became inventedFPS");
            records[40].m_nNumFramePresents=0;
            rate=unknown.rate(std::span<const Timing>(records));
            if(!rate||rate->output!=89)throw std::runtime_error("unshown source was counted");
        }
        {
            // A runtime may publish no timing record during xrEndFrame. Count
            // known successfully accepted images independently of that API.
            xrfg::AcceptedSceneRate accepted;
            xrfg::SceneIdentity id{1,1,0};
            for(unsigned i=0;i<=180;++i) {
                if(i && i*50/90>(i-1)*50/90)id={1,1+i*50/90,unsigned((i*50/90)%2)};
                accepted.accepted(std::int64_t(i)*1'000'000'000/90,id);
            }
            auto r=accepted.rate(2'000'000'000);
            if(!r||r->output!=50||r->real!=25||r->generated!=25)
                throw std::runtime_error("accepted total includes held resubmissions");
            xrfg::AcceptedSceneRate raw;unsigned serial=1;
            for(unsigned i=0;i<=180;++i) {
                if(i&&i*47/90>(i-1)*47/90)++serial;
                raw.accepted(std::int64_t(i)*1'000'000'000/90,{2,serial,0});
            }
            r=raw.rate(2'000'000'000);
            if(!r||r->output!=47)throw std::runtime_error("AFW-only held sources inflate total");
            raw.accepted(2'100'000'000,{});
            if(raw.rate(2'200'000'000))throw std::runtime_error("unknown identity invents new content");
            // Explicitly separate accepted deliveries from display proof.
            xrfg::InterfaceTelemetry t;t.rates_valid=true;t.output_fps=50;
            t.output_source=xrfg::InterfaceSource::accepted_scene_submissions;
            if(!xrfg::output_rate_valid(t)||xrfg::presented_output_valid(t))
                throw std::runtime_error("accepted images misrepresented as physical presentation");
            t.output_source=xrfg::InterfaceSource::runtime_accepted_delivery;
            if(!xrfg::output_rate_valid(t)||xrfg::presented_output_valid(t))
                throw std::runtime_error("VDXR native Ends misrepresented as physical scanout");
        }
        struct Delivery {
            std::optional<float> rate{45};unsigned calls{};
            std::optional<float> delivered_fps(std::int64_t) noexcept {++calls;return rate;}
        } delivery;
        xrfg::InterfaceTelemetry telemetry;telemetry.rates_valid=true;
        telemetry.output_fps=90;telemetry.output_source=xrfg::InterfaceSource::accepted_submissions;
        xrfg::poll_presented_output(&delivery,1,telemetry);
        if(delivery.calls!=1 || telemetry.output_fps!=45 || !xrfg::presented_output_valid(telemetry))
            throw std::runtime_error("meter did not poll actual delivery independently of small overlay");
        delivery.rate.reset();xrfg::poll_presented_output(&delivery,2,telemetry);
        if(delivery.calls!=2 || xrfg::presented_output_valid(telemetry))
            throw std::runtime_error("unavailable delivery became submitted FPS");
        delivery.rate=-1;xrfg::poll_presented_output(&delivery,3,telemetry);
        if(xrfg::presented_output_valid(telemetry))throw std::runtime_error("negative delivery accepted");
        delivery.rate=0;xrfg::poll_presented_output(&delivery,4,telemetry);
        if(!xrfg::presented_output_valid(telemetry) || telemetry.output_fps!=0)
            throw std::runtime_error("zero real presentation hidden");
        std::vector<Timing> full, repeated, dropped, irregular;
        for (unsigned i = 0; i <= 90; ++i) {
            full.push_back({i, 1, i % 5 < 2 ? 1U : 0U, i / 90.0});
            dropped.push_back({i, i % 2 ? 0U : 1U, 0, i / 90.0});
            if (i % 2 == 0) repeated.push_back({i, 2, 1, i / 90.0});
            if (i == 0 || (i * 76 / 90 > (i - 1) * 76 / 90))
                irregular.push_back({i, 1, i % 3 == 0 ? 1U : 0U, i / 90.0});
        }
        check(full, 90); check(repeated, 45); check(dropped, 45); check(irregular, 76);
        full.push_back({91, 0, 0, 91 / 90.0});
        full.push_back({92, 0, 0, 92 / 90.0});
        check(full, 90); // latest pending frame must not be counted as a loss
        for (unsigned start : {0U, 22U}) {
            std::vector<Timing> window(full.begin() + start, full.begin() + start + 64);
            check(window, 90); // overlapping 250ms polls retain full rate
        }
        full[15].m_nFrameIndex = full[14].m_nFrameIndex;
        if (xrfg::compositor_presentation_rate(std::span<const Timing>(full)))
            throw std::runtime_error("duplicate record accepted");
        std::cout << "Presented vsyncs, repeats, true omissions, 76/90 cadence and unsettled tails pass\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
