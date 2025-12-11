#include "signaling_client.h"

SignalingClient::SignalingClient(WebRTCPushClient *rtc_client, QObject *parent)
    : QObject(parent), m_rtcClient(rtc_client)
{
    connect(&m_webSocket, &QWebSocket::connected, this, &SignalingClient::onConnected);
    connect(&m_webSocket, &QWebSocket::textMessageReceived, this, &SignalingClient::onTextMessageReceived);
    connect(&m_webSocket, &QWebSocket::disconnected, this, &SignalingClient::onDisconnected);

    setupCallbacks();
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
    m_rtcClient->CreateAndSendOffer();
}

void SignalingClient::onTextMessageReceived(const QString &message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject())
        return;

    QJsonObject json = doc.object();
    QString type = json["type"].toString();

    if (type == "answer")
    {
        QString sdp = json["sdp"].toString();
        qDebug() << "Received Remote Answer";
        m_rtcClient->SetRemoteAnswer(sdp.toStdString());
    }
    else if (type == "candidate")
    {
        QString candidate = json["candidate"].toString();
        QString sdpMid = json["sdpMid"].toString();
        int sdpMLineIndex = json["sdpMLineIndex"].toInt();

        qDebug() << "Received Remote ICE";
        // 注意：AddRemoteIce 需要 sdpMid 等参数，之前的接口只留了 string
        // 你可能需要修改 WebRTCPushClient::AddRemoteIce 签名来接收更多参数
        // 这里假设只传 candidate 字符串，或者你修改底层接口适配
        m_rtcClient->AddRemoteIce(candidate.toStdString(), sdpMLineIndex, sdpMid.toStdString());
    }
}

void SignalingClient::onDisconnected()
{
    qDebug() << "Signaling disconnected!";
}

void SignalingClient::setupCallbacks()
{
    if (!m_rtcClient)
        return;

    // 1. 当 WebRTC 生成本地 Offer 时，通过 WebSocket 发送
    m_rtcClient->signaling.onLocalSdp = [this](const SdpBundle &bundle)
    {
        // 注意：这里是在 WebRTC 线程回调的，建议通过 Qt 的信号槽或 invokeMethod 转到主线程发送
        // 简单起见，QWebSocket 是线程安全的（write 操作），但最好用 QMetaObject::invokeMethod
        QJsonObject json;
        json["type"] = QString::fromStdString(bundle.type); // "offer"
        json["sdp"] = QString::fromStdString(bundle.sdp);

        QMetaObject::invokeMethod(this, [this, json]()
                                  { sendJson(json); });
    };

    // 2. 当 WebRTC 收集到本地 ICE 时，通过 WebSocket 发送
    m_rtcClient->signaling.onLocalIce = [this](const std::string &candidate)
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
