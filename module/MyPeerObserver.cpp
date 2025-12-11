#include "MyPeerObserver.h"

void MyPeerObserver::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState)
{
}
void MyPeerObserver::OnIceCandidate(const webrtc::IceCandidateInterface *candidate)
{
    std::string sdp;
    candidate->ToString(&sdp);
    QString msg = QString("{\"type\":\"candidate\",\"candidate\":\"%1\"}").arg(QString::fromStdString(sdp));
    wsClient->sendMessage(msg);
}