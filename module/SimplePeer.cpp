#include "SimplePeer.h"
#include "api/media_stream_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"

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

using webrtc::DataChannelInterface;
using webrtc::PeerConnectionInterface;
using webrtc::PeerConnectionObserver;

const char kAudioLabel[] = "audio_label";
const char kVideoLabel[] = "video_label";
const char kStreamId[] = "stream_id";

SimplePeer::SimplePeer(const webrtc::Environment& env, SendSignalingMessageCallback send_cb)
    : env_(env), send_cb_(std::move(send_cb))
{
}

bool SimplePeer::Init()
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
    pc_factory_ =
        webrtc::CreateModularPeerConnectionFactory(std::move(deps));

    auto sender_caps = pc_factory_->GetRtpSenderCapabilities(webrtc::MediaType::VIDEO);
    RTC_LOG(LS_INFO) << "Sender video codecs:";
    for (const auto &c : sender_caps.codecs)
    {
        printf("name: %s\n", c.name.c_str());
    }

    // auto receiver_caps = pc_factory_->GetRtpReceiverCapabilities(webrtc::MediaType::VIDEO);
    // RTC_LOG(LS_INFO) << "Receiver video codecs:";
    // for (const auto &c : receiver_caps.codecs)
    // {
    // }
    if (!pc_factory_)
    {
        RTC_LOG(LS_ERROR) << "Failed to create PeerConnectionFactory";
        return false;
    }

    pc_ = CreatePeerConnection();
    if (!pc_)
    {
        return false;
    }

    // 添加本地媒体轨道（需要平台实现）
    AddLocalMediaTracks();
    return true;
}

void SimplePeer::CreateOffer()
{
    if (!pc_)
    {
        return;
    }
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    // options.offer_to_receive_audio / video 已经过时，使用 transceivers 或 AddTrack
    pc_->CreateOffer(
        this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
}

SimplePeer::~SimplePeer()
{
    if (pc_)
    {
        pc_->Close();
        pc_ = nullptr;
    }
    pc_factory_ = nullptr;
}

webrtc::RefCountReleaseStatus SimplePeer::Release() const
{
    return webrtc::RefCountReleaseStatus();
}

void SimplePeer::OnRemoteDescription(const std::string &sdp, const std::string &type)
{
    
}

void SimplePeer::SendData(const std::string &msg)
{
}

void SimplePeer::OnRemoteIceCandidate(const std::string &sdpMid, int sdpMlineIndex, const std::string &sdp)
{
}

void SimplePeer::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state)
{
}

void SimplePeer::OnRemoveStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream)
{
}

void SimplePeer::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state)
{
}

void SimplePeer::OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
{
}

void SimplePeer::AddLocalMediaTracks()
{
}

webrtc::scoped_refptr<webrtc::PeerConnectionInterface> SimplePeer::CreatePeerConnection()
{

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    webrtc::PeerConnectionInterface::IceServer server;
    server.uri = "stun:stun.l.google.com:19302";
    config.servers.push_back(server);

    webrtc::PeerConnectionDependencies deps(this);
    auto result = pc_factory_->CreatePeerConnectionOrError(config, std::move(deps));
    if (!result.ok())
    {
        RTC_LOG(LS_ERROR) << "CreatePeerConnection failed: " << result.error().message();
        return nullptr;
    }
    return result.MoveValue();
}

void SimplePeer::OnMessage(const webrtc::DataBuffer &buffer)
{
}

void SimplePeer::OnSuccess(webrtc::SessionDescriptionInterface *desc)
{
}

void SimplePeer::OnFailure(webrtc::RTCError error)
{
}

void SimplePeer::AddRef() const
{
}

void SimplePeer::OnStateChange()
{
}

void SimplePeer::OnIceCandidate(const webrtc::IceCandidateInterface *candidate)
{
}

void SimplePeer::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state)
{
}

void SimplePeer::OnRenegotiationNeeded()
{
}

void SimplePeer::OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel)
{
}

void SimplePeer::OnAddStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream)
{
}
