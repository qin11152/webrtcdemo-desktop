#pragma once

#include <QObject>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QUrl>
#include <string>
#include "pushclient.h"

class SignalingClient : public QObject {
    Q_OBJECT
public:
    // 传入 WebRTC 客户端指针以便相互调用
    explicit SignalingClient(WebRTCPushClient* rtc_client, QObject* parent = nullptr);
    ~SignalingClient() = default;
    // 连接信令服务器
    void connectToServer(const QString& url);

private slots:
    void onConnected();
    void onTextMessageReceived(const QString& message);
    void onDisconnected();

private:
    // 绑定到 WebRTCPushClient 的回调
    void setupCallbacks();
    
    // 发送 JSON 辅助函数
    void sendJson(const QJsonObject& json);

    QWebSocket m_webSocket;
    WebRTCPushClient* m_rtcClient;
};