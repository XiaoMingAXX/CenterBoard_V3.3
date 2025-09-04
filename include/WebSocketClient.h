#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "SensorData.h"

// 前向声明
class BufferPool;

// WebSocket客户端类，处理与服务器的通信
class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();
    
    // 初始化网络连接
    bool initialize(const char* ssid, const char* password, const char* serverUrl, uint16_t port, const char* deviceCode);
    
    // 连接到服务器
    bool connect();
    
    // 断开连接
    void disconnect();
    
    // 发送数据块
    bool sendDataBlock(DataBlock* block);
    
    // 处理服务器命令
    void handleServerCommand(const String& command);
    
    // 获取连接状态
    bool isConnected() const;
    
    // 获取统计信息
    struct Stats {
        uint32_t totalBlocksSent;
        uint32_t totalBytesSent;
        uint32_t connectionAttempts;
        uint32_t connectionFailures;
        uint32_t sendFailures;
        float avgSendRate;
        uint32_t lastHeartbeat;
        bool serverConnected;
    };
    Stats getStats() const;
    
    // 重置统计信息
    void resetStats();
    
    // 设置设备信息
    void setDeviceInfo(const String& deviceCode, const String& sessionId);
    
    // 开始数据采集
    void startCollection();
    
    // 停止数据采集
    void stopCollection();
    
    // 发送心跳
    void sendHeartbeat();
    
    // 主循环处理
    void loop();
    
    // 处理发送队列
    void processSendQueue();
    
    // 处理连接重试
    void handleConnectionRetry();
    
    // 设置BufferPool实例用于正确释放数据块
    void setBufferPool(BufferPool* bufferPool);
    
    // 手动设置连接状态（用于调试）
    void setConnectionStatus(bool connected);
    
    // 发送上传完成消息
    void sendUploadComplete();
    
private:
    WebSocketsClient webSocket;
    String serverUrl;
    uint16_t serverPort;
    String deviceCode;
    String sessionId;
    bool collectionActive;
    bool uploadCompletePending;  // 标记是否等待发送upload_complete消息
    SemaphoreHandle_t mutex;
    Stats stats;
    uint32_t lastStatsTime;
    uint32_t blocksSentSinceLastStats;
    
    // 网络状态
    bool wifiConnected;
    bool serverConnected;
    uint32_t lastConnectionAttempt;
    uint32_t connectionRetryInterval;
    
    // 数据发送相关
    QueueHandle_t sendQueue;
    static const size_t MAX_QUEUE_SIZE = 20;
    
    // BufferPool实例，用于正确释放数据块
    BufferPool* bufferPool;
    
    // 创建JSON数据包
    String createDataPacket(DataBlock* block);
    
    // 处理WebSocket事件
    static void webSocketEvent(WStype_t type, uint8_t* payload, size_t length);
    
    // 解析服务器命令
    void parseServerCommand(const String& jsonCommand);
    
    // 发送ACK响应
    void sendAckResponse(const String& commandId, bool success);
    
    // 发送状态响应
    void sendStatusResponse(const String& commandId);
    
    // 更新统计信息
    void updateStats();
};

#endif // WEBSOCKET_CLIENT_H
