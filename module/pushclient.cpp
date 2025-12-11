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
    src->running_ = true;
    src->cap_thread_ = std::thread([src, target_fps, capture_cursor]()
                                   { src->StartCaptureLoop(target_fps, capture_cursor); });
    return src;
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
        printf("received frame: %dx%d\n", frame->size().width(), frame->size().height());
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

WebRTCPushClient::WebRTCPushClient()
{
    webrtc::InitializeSSL();
    network_thread_ = std::make_unique<webrtc::Thread>(nullptr);
    worker_thread_ = std::make_unique<webrtc::Thread>(nullptr);
    signaling_thread_ = std::make_unique<webrtc::Thread>(nullptr);
    network_thread_->Start();
    worker_thread_->Start();
    signaling_thread_->Start();
}

WebRTCPushClient::~WebRTCPushClient()
{
    pc_ = nullptr;
    factory_ = nullptr;
    signaling_thread_->Stop();
    worker_thread_->Stop();
    network_thread_->Stop();
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

    return true;
}

bool WebRTCPushClient::AddDesktopVideo(int fps, int max_bitrate_bps)
{
    auto source = DesktopCapturerSource::Create(fps);
    if (!source)
        return false;

    video_track_ = factory_->CreateVideoTrack(source, "desktop");
    if (!video_track_)
        return false;

    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
    auto transceiver_or = pc_->AddTransceiver(video_track_, init);
    if (!transceiver_or.ok())
    {
        RTC_LOG(LS_ERROR) << "AddTransceiver failed: " << transceiver_or.error().message();
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
                    signaling.onLocalSdp({"offer", sdp});
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
