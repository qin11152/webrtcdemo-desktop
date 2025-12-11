#pragma once

#include <memory>
#include <string>
#include <iostream>
#include <vector>
#include "absl/base/nullability.h"
#include "api/environment/environment.h"
#include "api/rtp_receiver_interface.h"

#include "api/peer_connection_interface.h"
#include "api/create_peerconnection_factory.h"
#include "api/data_channel_interface.h"
#include "api/scoped_refptr.h"
#include "api/media_stream_interface.h"
#include "api/rtc_error.h"
#include "rtc_base/thread.h"
#include "rtc_base/logging.h"

// 你的信令回调接口（应用层实现）
using SendSignalingMessageCallback = std::function<void(const std::string &jsonMessage)>;

class SimplePeer : public webrtc::PeerConnectionObserver,
                   public webrtc::CreateSessionDescriptionObserver,
                   public webrtc::DataChannelObserver
{
public:
    SimplePeer(const webrtc::Environment& env, SendSignalingMessageCallback send_cb);
    ~SimplePeer();
    // RefCountInterface requirement
    virtual webrtc::RefCountReleaseStatus Release() const override;

    // 初始化 PeerConnectionFactory 并创建 PeerConnection
    bool Init();

    // 发起端调用：创建 offer 并把 SDP 通过 send_cb 发送出去
    void CreateOffer();

    // 处理远端 SDP（offer/answer）
    void OnRemoteDescription(const std::string &sdp, const std::string &type);

    // 处理远端 ICE candidate JSON（textified candidate）
    void OnRemoteIceCandidate(const std::string &sdpMid, int sdpMlineIndex, const std::string &sdp);

    // 发送数据通道消息（如果已经 open）
    void SendData(const std::string &msg);

    // PeerConnectionObserver implementation
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;
    void OnAddStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
    void OnRemoveStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;
    void OnRenegotiationNeeded() override;
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
    void OnIceCandidate(const webrtc::IceCandidateInterface *candidate) override;
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;

    // DataChannelObserver
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer &buffer) override;

    // CreateSessionDescriptionObserver implementation.
    void OnSuccess(webrtc::SessionDescriptionInterface *desc) override;
    void OnFailure(webrtc::RTCError error) override;
    virtual void AddRef() const override;

private:
    // helper to create PeerConnection
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> CreatePeerConnection();

    // helper to create local audio/video tracks (platform dependent)
    void AddLocalMediaTracks();

    SendSignalingMessageCallback send_cb_;

    const webrtc::Environment env_;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
    webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel_;

    webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> local_video_source_;

    // threads
    std::unique_ptr<webrtc::Thread> network_thread_;
    std::unique_ptr<webrtc::Thread> worker_thread_;
    std::unique_ptr<webrtc::Thread> signaling_thread_;
};