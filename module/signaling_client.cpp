#include "signaling_client.h"

#include <nlohmann/json.hpp>

SignalingClient::SignalingClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_webSocket, &QWebSocket::connected, this, &SignalingClient::onConnected);
    connect(&m_webSocket, &QWebSocket::textMessageReceived, this, &SignalingClient::onTextMessageReceived);
    connect(&m_webSocket, &QWebSocket::disconnected, this, &SignalingClient::onDisconnected);

    // setupCallbacks();
}

void SignalingClient::connectToServer(const QString &url)
{
    qDebug() << "Connecting to signaling server:" << url;
    m_webSocket.open(QUrl(url));
}

void SignalingClient::onConnected()
{
    qDebug() << "Signaling connected!";
    // 连接成功后，立即创建并发送 Offer
    // 注意：要在 WebRTC 线程或确保线程安全，这里简单直接调用
    // m_rtcClient->CreateAndSendOffer();
}

void SignalingClient::onTextMessageReceived(const QString &message)
{

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(message.toUtf8().constData());
    } catch (const std::exception& e) 
    {
        printf("JSON parse failed: %s\n", e.what());
        return;
    }

    // 2. 取出 type 和 sdp
    std::string type = j.value("type", "");
    std::string sdp  = j.value("sdp", "");
    std::string id   = j.value("id", "");

    printf("Remote SDP type: %s\n", type.c_str());

    // printf("Signaling message received: %s\n", message.toUtf8().constData());
    // QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    // if (doc.isNull() || !doc.isObject())
    //     return;

    // QJsonObject json = doc.object();
    // QString type = json["type"].toString();

    // auto id = json["id"].toString();
    // qDebug() << "Received message of type:" << type << "from id:" << id;

    if (type == "answer")
    {
        std::string sdp = j.value("sdp", "");
        auto jt = clients.find(id);
        if (jt != clients.end())
        {
            jt->second->SetRemoteAnswer(sdp);
        }
        // m_rtcClient->SetRemoteAnswer(sdp.toStdString());
    }
    else if (type == "request")
    {
        clients[id] = std::make_shared<WebRTCPushClient>(id);
        std::vector<IceServerConfig> iceServers = {
            {"stun:stun.l.google.com:19302", "", ""}};
        setupCallbacks(clients[id]);
        clients[id]->Init(iceServers);
    }
    else if (type == "candidate")
    {
        std::string candidate = j.value("candidate", "");
        std::string sdpMid = j.value("sdpMid", "");
        int sdpMLineIndex = j.value("sdpMLineIndex", 0);

        qDebug() << "Received Remote ICE";
        // 注意：AddRemoteIce 需要 sdpMid 等参数，之前的接口只留了 string
        // 你可能需要修改 WebRTCPushClient::AddRemoteIce 签名来接收更多参数
        // 这里假设只传 candidate 字符串，或者你修改底层接口适配
        clients[id]->AddRemoteIce(candidate, sdpMLineIndex, sdpMid);
    }
}

void SignalingClient::onDisconnected()
{
    qDebug() << "Signaling disconnected!";
}

void SignalingClient::setupCallbacks(std::shared_ptr<WebRTCPushClient> rtcClient)
{
    // 1. 当 WebRTC 生成本地 Offer 时，通过 WebSocket 发送
    rtcClient->signaling.onLocalSdp = [this](const SdpBundle &bundle, std::string id)
    {
        // 注意：这里是在 WebRTC 线程回调的，建议通过 Qt 的信号槽或 invokeMethod 转到主线程发送
        // 简单起见，QWebSocket 是线程安全的（write 操作），但最好用 QMetaObject::invokeMethod
        QJsonObject json;
        json["type"] = QString::fromStdString(bundle.type); // "offer"
        json["sdp"] = QString::fromStdString(bundle.sdp);
        json["id"] = QString::fromStdString(id);

        QMetaObject::invokeMethod(this, [this, json]()
                                  { sendJson(json); });
    };

    // 2. 当 WebRTC 收集到本地 ICE 时，通过 WebSocket 发送
    rtcClient->signaling.onLocalIce = [this](const std::string &candidate)
    {
        QJsonObject json;
        json["type"] = "candidate";
        json["candidate"] = QString::fromStdString(candidate);
        json["sdpMid"] = "video"; // 简化的假设
        json["sdpMLineIndex"] = 0;

        QMetaObject::invokeMethod(this, [this, json]()
                                  { sendJson(json); });
    };
}

void SignalingClient::sendJson(const QJsonObject &json)
{
    if (m_webSocket.isValid())
    {
        QJsonDocument doc(json);
        m_webSocket.sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    }
}
