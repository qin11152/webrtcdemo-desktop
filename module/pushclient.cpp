#include "pushclient.h"

#include "api/audio_options.h"
#include "media/engine/webrtc_media_engine.h"
#include "media/engine/webrtc_video_engine.h"
#include "media/engine/webrtc_voice_engine.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/video_capture/video_capture_factory.h"
#include "api/rtp_transceiver_direction.h"
#include "api/jsep.h"
#include "rtc_base/ssl_adapter.h"
#include "libyuv.h"
#include "api/video/i420_buffer.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "rtc_base/time_utils.h"
#include "api/stats/rtc_stats.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

// 简化版 Observer 实现：CreateSessionDescriptionObserver/SetSessionDescriptionObserver
namespace webrtc
{
    class CreateSessionDescriptionObserverq : public webrtc::CreateSessionDescriptionObserver
    {
    public:
        using OnSuccessFn = std::function<void(SessionDescriptionInterface *)>;
        using OnFailureFn = std::function<void(RTCError)>;
        CreateSessionDescriptionObserverq(OnSuccessFn ok, OnFailureFn fail)
            : ok_(std::move(ok)), fail_(std::move(fail)) {}
        void OnSuccess(SessionDescriptionInterface *desc) override { ok_(desc); }
        void OnFailure(RTCError error) override { fail_(error); }

    private:
        OnSuccessFn ok_;
        OnFailureFn fail_;
    };

    class SetSessionDescriptionObserverq : public webrtc::SetSessionDescriptionObserver
    {
    public:
        void OnSuccess() override { RTC_LOG(LS_INFO) << "SetDescription OK"; }
        void OnFailure(RTCError error) override { RTC_LOG(LS_ERROR) << "SetDescription failed: " << error.message(); }
    };
} // namespace webrtc

webrtc::scoped_refptr<CapturerTrackSource> CapturerTrackSource::Create(int target_fps, bool capture_cursor)
{
    auto src = webrtc::make_ref_counted<CapturerTrackSource>();
    // 这里用 ScreenCapturer；如果要窗口捕获，改成 CreateWindowCapturer 并传 window id
    webrtc::DesktopCaptureOptions options = webrtc::DesktopCaptureOptions::CreateDefault();

    src->capturer_ = (webrtc::DesktopCapturer::CreateScreenCapturer(options));
    if (!src->capturer_)
    {
        RTC_LOG(LS_ERROR) << "Failed to create screen capturer";
        return nullptr;
    }

    auto list = webrtc::DesktopCapturer::SourceList{};
    src->capturer_->GetSourceList(&list);
    if (!list.empty())
    {
        src->capturer_->SelectSource(list[0].id);
    }

    return src;
}

CapturerTrackSource::CapturerTrackSource()
    : webrtc::VideoTrackSource(/*remote*/ false), running_(false)
{
}

void CapturerTrackSource::Start()
{
    running_ = true;
    cap_thread_ = std::thread([this]()
                              { StartCaptureLoop(25, true); });
}

void CapturerTrackSource::StartCaptureLoop(int target_fps, bool capture_cursor)
{
    class Callback : public webrtc::DesktopCapturer::Callback
    {
    public:
        explicit Callback(CapturerTrackSource *src) : src_(src) {}
        void OnCaptureResult(webrtc::DesktopCapturer::Result result, std::unique_ptr<webrtc::DesktopFrame> frame) override
        {
            if (result != webrtc::DesktopCapturer::Result::SUCCESS || !frame)
                return;
            // 将 DesktopFrame 转为 I420 VideoFrame
            int width = frame->size().width();
            int height = frame->size().height();

            // DesktopFrame 为 BGRA，简单起见用 libyuv 做转换（libwebrtc 已内置）
            webrtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(width, height);
            const uint8_t *src_bgra = frame->data();
            int src_stride_bgra = frame->stride();

            // BGRA -> I420
            libyuv::ARGBToI420(src_bgra, src_stride_bgra,
                               i420->MutableDataY(), i420->StrideY(),
                               i420->MutableDataU(), i420->StrideU(),
                               i420->MutableDataV(), i420->StrideV(),
                               width, height);

            webrtc::VideoFrame vf = webrtc::VideoFrame::Builder()
                                        .set_video_frame_buffer(i420)
                                        .set_timestamp_us(webrtc::TimeMicros())
                                        .build();

            // src_->OnFrame(vf);
            src_->OnCapturedFrame(vf);
        }

    private:
        CapturerTrackSource *src_;
    };

    Callback cb(this);
    capturer_->Start(&cb);

    const int interval_ms = 1000 / std::max(1, target_fps);
    while (running_)
    {
        capturer_->CaptureFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

webrtc::scoped_refptr<DesktopCapturerSource> DesktopCapturerSource::Create(bool is_screen)
{
    // return webrtc::scoped_refptr<DesktopCapturerSource>(
    //     new webrtc::RefCountedObject<DesktopCapturerSource>(is_screen)
    // );
    auto src = webrtc::make_ref_counted<DesktopCapturerSource>(is_screen);
    return src;
    // auto src = new DesktopCapturerSource(is_screen);
    // return nullptr;
}

DesktopCapturerSource::DesktopCapturerSource(bool is_screen) : is_running_(false), fps_(30)
{
    webrtc::DesktopCaptureOptions options = webrtc::DesktopCaptureOptions::CreateDefault();

    if (is_screen)
    {
        capturer_ = webrtc::DesktopCapturer::CreateScreenCapturer(options);
        auto list = webrtc::DesktopCapturer::SourceList{};
        capturer_->GetSourceList(&list);
        if (!list.empty())
        {
            capturer_->SelectSource(list[0].id);
        }
    }
    else
    {
        capturer_ = webrtc::DesktopCapturer::CreateWindowCapturer(options);
    }

    if (capturer_)
    {
        capturer_->Start(this);
    }
}

DesktopCapturerSource::~DesktopCapturerSource()
{
    Stop();
}

void DesktopCapturerSource::Start()
{
    if (is_running_)
        return;
    is_running_ = true;
    capture_thread_.reset(new std::thread(&DesktopCapturerSource::CaptureLoop, this));
}

void DesktopCapturerSource::Stop()
{
    is_running_ = false;
    if (capture_thread_ && capture_thread_->joinable())
    {
        capture_thread_->join();
    }
}

void DesktopCapturerSource::OnCaptureResult(webrtc::DesktopCapturer::Result result, std::unique_ptr<webrtc::DesktopFrame> frame)
{
    if (result == webrtc::DesktopCapturer::Result::SUCCESS && frame)
    {
        // 将 DesktopFrame 转换为 VideoFrame 并传递给基类
        // printf("received frame: %dx%d\n", frame->size().width(), frame->size().height());
    }
}

void DesktopCapturerSource::CaptureLoop()
{
    while (is_running_)
    {
        if (capturer_)
        {
            capturer_->CaptureFrame();
        }
        // 简单的帧率控制
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps_));
    }
}

WebRTCPushClient::WebRTCPushClient(std::string id)
    : id{id}
{
    webrtc::InitializeSSL();
    signaling_thread_ = webrtc::Thread::CreateWithSocketServer();
    signaling_thread_->Start();
}

WebRTCPushClient::~WebRTCPushClient()
{
    StopRtpSendStatsPolling();
    pc_ = nullptr;
    factory_ = nullptr;
    signaling_thread_->Stop();
    signaling_thread_ = nullptr;
    webrtc::CleanupSSL();
}

bool WebRTCPushClient::Init(const std::vector<IceServerConfig> &ice_servers)
{
    webrtc::PeerConnectionFactoryDependencies deps;
    deps.signaling_thread = signaling_thread_.get();
    // deps.env = env_,
    deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
    deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
    deps.video_encoder_factory =
        std::make_unique<webrtc::VideoEncoderFactoryTemplate<
            webrtc::LibvpxVp8EncoderTemplateAdapter,
            webrtc::LibvpxVp9EncoderTemplateAdapter,
            webrtc::OpenH264EncoderTemplateAdapter,
            webrtc::LibaomAv1EncoderTemplateAdapter>>();
    deps.video_decoder_factory =
        std::make_unique<webrtc::VideoDecoderFactoryTemplate<
            webrtc::LibvpxVp8DecoderTemplateAdapter,
            webrtc::LibvpxVp9DecoderTemplateAdapter,
            webrtc::OpenH264DecoderTemplateAdapter,
            webrtc::Dav1dDecoderTemplateAdapter>>();
    webrtc::EnableMedia(deps);
    factory_ =
        webrtc::CreateModularPeerConnectionFactory(std::move(deps));

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    // webrtc::PeerConnectionInterface::IceServer server;
    // server.uri = ice_servers[0].uri;
    // config.servers.push_back(server);

    // 1) 配置 ICE 服务器（STUN/TURN）
    webrtc::PeerConnectionInterface::IceServer stun;
    stun.urls = {"stun:stun.l.google.com:19302"};
    config.servers.push_back(stun);

    // 2) 候选过滤与传输类型
    // 仅收集/使用某些类型的候选（可选）：
    config.type = webrtc::PeerConnectionInterface::IceTransportsType::kAll;
    // 可改为 kRelay（只走 TURN），kNone（禁用）等

    // 3) 持续收集策略（如果你想水位更稳定，或长期收集）：
    config.continual_gathering_policy =
        webrtc::PeerConnectionInterface::ContinualGatheringPolicy::GATHER_CONTINUALLY;

    // 4) 网络相关（根据需要开启/关闭 IPv6、网关选择等）
    config.disable_ipv6_on_wifi = false;

    observer_ = std::make_unique<PeerObserver>(&signaling, this);

    webrtc::PeerConnectionDependencies pc_dependencies(observer_.get());
    auto error_or_peer_connection =
        factory_->CreatePeerConnectionOrError(
            config, std::move(pc_dependencies));
    if (error_or_peer_connection.ok())
    {
        pc_ = std::move(error_or_peer_connection.value());
    }

    AddDesktopVideo(30, 2000000);

    CreateAndSendOffer();

    return true;
}

bool WebRTCPushClient::AddDesktopVideo(int fps, int max_bitrate_bps)
{
    auto source = CapturerTrackSource::Create(fps);
    if (!source)
    {
        printf("Failed to create DesktopCapturerSource\n");
        return false;
    }

    video_track_ = factory_->CreateVideoTrack(source, "desktop");
    if (!video_track_)
    {
        printf("Failed to create VideoTrack\n");
        return false;
    }

    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
    auto transceiver_or = pc_->AddTransceiver(video_track_, init);
    if (!transceiver_or.ok())
    {
        RTC_LOG(LS_ERROR) << "AddTransceiver failed: " << transceiver_or.error().message();
        printf("AddTransceiver failed\n");
        return false;
    }
    auto transceiver = transceiver_or.value();
    video_sender_ = transceiver->sender();

    // 设置码率上限
    if (max_bitrate_bps > 0)
    {
        webrtc::RtpParameters params = video_sender_->GetParameters();
        if (!params.encodings.empty())
        {
            params.encodings[0].max_bitrate_bps = max_bitrate_bps;
            video_sender_->SetParameters(params);
        }
    }

    source->Start();
    return true;
}

bool WebRTCPushClient::CreateAndSendOffer(bool ice_restart)
{
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions opts;
    opts.offer_to_receive_audio = 0;
    opts.offer_to_receive_video = 0;
    opts.ice_restart = ice_restart;

    pc_->CreateOffer(
        new webrtc::RefCountedObject<webrtc::CreateSessionDescriptionObserverq>(
            [this](webrtc::SessionDescriptionInterface *desc)
            {
                pc_->SetLocalDescription(
                    new webrtc::RefCountedObject<webrtc::SetSessionDescriptionObserverq>(),
                    desc);

                std::string sdp;
                desc->ToString(&sdp);
                if (signaling.onLocalSdp)
                    signaling.onLocalSdp({"offer", sdp}, id);
                RTC_LOG(LS_INFO) << "Local Offer:\n"
                                 << sdp;
            },
            [](webrtc::RTCError err)
            {
                RTC_LOG(LS_ERROR) << "CreateOffer failed: " << err.message();
            }),
        opts);
    return true;
}

bool WebRTCPushClient::SetRemoteAnswer(const std::string &sdp_answer)
{
    printf("%s\n", sdp_answer.c_str());
    return false;
    auto desc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp_answer);
    if (!desc)
    {
        RTC_LOG(LS_ERROR) << "Invalid remote answer SDP";
        return false;
    }
    pc_->SetRemoteDescription(
        new webrtc::RefCountedObject<webrtc::SetSessionDescriptionObserverq>(),
        desc.release());
    return true;
}

bool WebRTCPushClient::AddRemoteIce(const std::string &candidate_sdp, int sdp_mline_index, const std::string &sdp_mid)
{
    webrtc::SdpParseError err;
    std::unique_ptr<webrtc::IceCandidateInterface> cand(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate_sdp, &err));
    if (!cand)
    {
        RTC_LOG(LS_ERROR) << "Parse ICE failed: " << err.description;
        return false;
    }
    bool ok = pc_->AddIceCandidate(cand.get());
    RTC_LOG(LS_INFO) << "AddRemoteIce: " << ok;
    return ok;
}

bool WebRTCPushClient::SetMaxBitrate(int bps)
{
    if (!video_sender_)
        return false;
    auto params = video_sender_->GetParameters();
    if (params.encodings.empty())
        params.encodings.push_back(webrtc::RtpEncodingParameters());
    params.encodings[0].max_bitrate_bps = bps;
    return video_sender_->SetParameters(params).ok();
}

void WebRTCPushClient::StartRtpSendStatsPolling(int interval_ms)
{
    if (stats_polling_.exchange(true))
    {
        return; // already running
    }

    is_sending_rtp_video_.store(false);
    last_video_bytes_sent_.store(0);
    last_video_packets_sent_.store(0);

    stats_thread_ = std::make_unique<std::thread>([this, interval_ms]()
                                                  {
        const int sleep_ms = std::max(100, interval_ms);
        while (stats_polling_.load())
        {
            PollRtpSendStatsOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        } });
}

void WebRTCPushClient::StopRtpSendStatsPolling()
{
    stats_polling_.store(false);
    if (stats_thread_ && stats_thread_->joinable())
    {
        stats_thread_->join();
    }
    stats_thread_.reset();
}

void WebRTCPushClient::PollRtpSendStatsOnce()
{
    if (!pc_)
        return;

    // 未连接时没必要判定 RTP
    if (pc_->peer_connection_state() != webrtc::PeerConnectionInterface::PeerConnectionState::kConnected)
    {
        is_sending_rtp_video_.store(false);
        return;
    }

    class StatsCallback final : public webrtc::RTCStatsCollectorCallback {
    public:
        explicit StatsCallback(WebRTCPushClient *owner) : owner_(owner) {}

        void OnStatsDelivered(const webrtc::scoped_refptr<const webrtc::RTCStatsReport> &report) override
        {
            if (!owner_)
                return;

            uint64_t best_bytes_sent = 0;
            uint64_t best_packets_sent = 0;
            bool found_video_outbound = false;

            // 强类型遍历 outbound-rtp
            for (const webrtc::RTCOutboundRtpStreamStats *s : report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>())
            {
                if (!s || !s->kind || *s->kind != "video")
                    continue;
                if (!s->bytes_sent)
                    continue;

                found_video_outbound = true;
                if (*s->bytes_sent >= best_bytes_sent)
                {
                    best_bytes_sent = *s->bytes_sent;
                    if (s->packets_sent)
                        best_packets_sent = *s->packets_sent;
                }
            }

            if (!found_video_outbound)
            {
                owner_->is_sending_rtp_video_.store(false);
                RTC_LOG(LS_INFO) << "[RTP-STATS] outbound-rtp(video) not found";
                return;
            }

            uint64_t last_bytes = owner_->last_video_bytes_sent_.exchange(best_bytes_sent);
            uint64_t last_packets = owner_->last_video_packets_sent_.exchange(best_packets_sent);

            const bool sending = (best_bytes_sent > last_bytes);
            owner_->is_sending_rtp_video_.store(sending);

            RTC_LOG(LS_INFO) << "[RTP-STATS] video outbound bytesSent=" << best_bytes_sent
                             << " (delta=" << (best_bytes_sent - last_bytes) << ")"
                             << " packetsSent=" << best_packets_sent
                             << " (delta=" << (best_packets_sent - last_packets) << ")"
                             << " sending=" << (sending ? "YES" : "NO");
        }

        void AddRef() const override {}
        webrtc::RefCountReleaseStatus Release() const override
        {
            delete this;
            return webrtc::RefCountReleaseStatus::kDroppedLastRef;
        }

    private:
        WebRTCPushClient *owner_;
    };

    pc_->GetStats(new StatsCallback(this));
}

void PeerObserver::OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state)
{
    RTC_LOG(LS_INFO) << "PeerConnection state: " << new_state;
    if (!owner_)
        return;
    if (new_state == webrtc::PeerConnectionInterface::PeerConnectionState::kConnected)
    {
        owner_->StartRtpSendStatsPolling(1000);
    }
    else if (new_state == webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected ||
             new_state == webrtc::PeerConnectionInterface::PeerConnectionState::kFailed ||
             new_state == webrtc::PeerConnectionInterface::PeerConnectionState::kClosed)
    {
        owner_->StopRtpSendStatsPolling();
    }
}
