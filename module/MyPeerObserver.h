#pragma once

#include "module/WebsocketClient.h"

// WebRTC 相关头文件
#include "api/peer_connection_interface.h"

class MyPeerObserver : public webrtc::PeerConnectionObserver {
public:
    MyPeerObserver(WebsocketClient* ws) : wsClient(ws) {}

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override;
    void OnAddStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>) override {}
    void OnRemoveStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnRenegotiationNeeded() override {}
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;

private:
    WebsocketClient* wsClient;
};
