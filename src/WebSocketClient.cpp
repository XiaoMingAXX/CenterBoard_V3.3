#include "WebSocketClient.h"
#include <ArduinoJson.h>

WebSocketClient::WebSocketClient() {
    serverPort = 8080;
    collectionActive = false;
    wifiConnected = false;
    serverConnected = false;
    lastConnectionAttempt = 0;
    connectionRetryInterval = 5000; // 5秒重试间隔
    
    mutex = xSemaphoreCreateMutex();
    sendQueue = xQueueCreate(MAX_QUEUE_SIZE, sizeof(DataBlock*));
    
    memset(&stats, 0, sizeof(stats));
    lastStatsTime = millis();
    blocksSentSinceLastStats = 0;
    
    Serial.printf("[WebSocketClient] Created\n");
}

WebSocketClient::~WebSocketClient() {
    disconnect();
    
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    
    if (sendQueue) {
        vQueueDelete(sendQueue);
    }
}

bool WebSocketClient::initialize(const char* ssid, const char* password, const char* url, uint16_t port) {
    serverUrl = String(url);
    serverPort = port;
    
    // 初始化WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.printf("[WebSocketClient] Connecting to WiFi: %s\n", ssid);
    
    // 等待WiFi连接
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
        Serial.printf("[WebSocketClient] WiFi connection attempt %d\n", attempts);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("[WebSocketClient] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        wifiConnected = false;
        Serial.printf("[WebSocketClient] ERROR: WiFi connection failed\n");
        return false;
    }
    
    // 初始化WebSocket
    webSocket.begin(serverUrl.c_str(), serverPort, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
    
    Serial.printf("[WebSocketClient] Initialized. Server: %s:%d\n", serverUrl.c_str(), serverPort);
    return true;
}

bool WebSocketClient::connect() {
    if (!wifiConnected) {
        Serial.printf("[WebSocketClient] ERROR: WiFi not connected\n");
        return false;
    }
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        lastConnectionAttempt = millis();
        stats.connectionAttempts++;
        
        webSocket.begin(serverUrl.c_str(), serverPort, "/");
        webSocket.onEvent(webSocketEvent);
        
        xSemaphoreGive(mutex);
    }
    
    return true;
}

void WebSocketClient::disconnect() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        webSocket.disconnect();
        serverConnected = false;
        collectionActive = false;
        xSemaphoreGive(mutex);
    }
    
    Serial.printf("[WebSocketClient] Disconnected\n");
}

bool WebSocketClient::sendDataBlock(DataBlock* block) {
    if (!block || !serverConnected || !collectionActive) {
        return false;
    }
    
    // 将数据块加入发送队列
    if (xQueueSend(sendQueue, &block, 0) != pdTRUE) {
        Serial.printf("[WebSocketClient] WARNING: Send queue full, dropping block\n");
        return false;
    }
    
    return true;
}

void WebSocketClient::handleServerCommand(const String& command) {
    parseServerCommand(command);
}

bool WebSocketClient::isConnected() const {
    return serverConnected && wifiConnected;
}

WebSocketClient::Stats WebSocketClient::getStats() const {
    return stats;
}

void WebSocketClient::resetStats() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        memset(&stats, 0, sizeof(stats));
        lastStatsTime = millis();
        blocksSentSinceLastStats = 0;
        xSemaphoreGive(mutex);
    }
}

void WebSocketClient::setDeviceInfo(const String& deviceCode, const String& sessionId) {
    this->deviceCode = deviceCode;
    this->sessionId = sessionId;
    Serial.printf("[WebSocketClient] Device info set: %s, Session: %s\n", 
                  deviceCode.c_str(), sessionId.c_str());
}

void WebSocketClient::startCollection() {
    collectionActive = true;
    Serial.printf("[WebSocketClient] Data collection started\n");
}

void WebSocketClient::stopCollection() {
    collectionActive = false;
    Serial.printf("[WebSocketClient] Data collection stopped\n");
}

void WebSocketClient::sendHeartbeat() {
    if (!serverConnected) {
        return;
    }
    
    StaticJsonDocument<200> doc;
    doc["type"] = "heartbeat";
    doc["device_code"] = deviceCode;
    doc["timestamp"] = millis();
    
    String message;
    serializeJson(doc, message);
    
    webSocket.sendTXT(message);
    stats.lastHeartbeat = millis();
}

void WebSocketClient::loop() {
    webSocket.loop();
}

String WebSocketClient::createDataPacket(DataBlock* block) {
    StaticJsonDocument<2048> doc;
    
    // 创建批量数据数组
    JsonArray batchData = doc.createNestedArray("batch_data");
    
    for (int i = 0; i < block->frameCount; i++) {
        JsonObject frame = batchData.createNestedObject();
        
        // 加速度数据
        JsonArray acc = frame.createNestedArray("acc");
        acc.add(block->frames[i].acc[0]);
        acc.add(block->frames[i].acc[1]);
        acc.add(block->frames[i].acc[2]);
        
        // 角速度数据
        JsonArray gyro = frame.createNestedArray("gyro");
        gyro.add(block->frames[i].gyro[0]);
        gyro.add(block->frames[i].gyro[1]);
        gyro.add(block->frames[i].gyro[2]);
        
        // 角度数据
        JsonArray angle = frame.createNestedArray("angle");
        angle.add(block->frames[i].angle[0]);
        angle.add(block->frames[i].angle[1]);
        angle.add(block->frames[i].angle[2]);
        
        // 时间戳
        frame["timestamp"] = block->frames[i].timestamp;
    }
    
    // 添加元数据
    doc["device_code"] = deviceCode;
    doc["sensor_type"] = SensorData::getSensorType(block->frames[0].sensorId);
    if (sessionId.length() > 0) {
        doc["session_id"] = sessionId;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

void WebSocketClient::webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    // 这是一个静态回调函数，需要通过全局变量或其他方式访问实例
    // 这里简化处理，实际实现中需要更复杂的实例管理
    Serial.printf("[WebSocketClient] WebSocket event: %d\n", type);
    
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WebSocketClient] Disconnected\n");
            break;
            
        case WStype_CONNECTED:
            Serial.printf("[WebSocketClient] Connected to server\n");
            break;
            
        case WStype_TEXT:
            Serial.printf("[WebSocketClient] Received: %s\n", payload);
            // TODO: 处理服务器命令
            break;
            
        case WStype_ERROR:
            Serial.printf("[WebSocketClient] WebSocket error\n");
            break;
    }
}

void WebSocketClient::parseServerCommand(const String& jsonCommand) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonCommand);
    
    if (error) {
        Serial.printf("[WebSocketClient] ERROR: Failed to parse server command: %s\n", error.c_str());
        return;
    }
    
    String commandType = doc["type"] | doc["command"] | "";
    
    if (commandType == "start_collection") {
        deviceCode = doc["device_code"] | "";
        sessionId = doc["session_id"] | "";
        startCollection();
        Serial.printf("[WebSocketClient] Start collection command received\n");
        
    } else if (commandType == "STOP_COLLECTION") {
        stopCollection();
        Serial.printf("[WebSocketClient] Stop collection command received\n");
        
    } else if (commandType == "SYNC") {
        // TODO: 处理时间同步命令
        Serial.printf("[WebSocketClient] Sync command received\n");
        
    } else if (commandType == "SET_BATCH") {
        // TODO: 处理批量大小设置命令
        Serial.printf("[WebSocketClient] Set batch command received\n");
        
    } else {
        Serial.printf("[WebSocketClient] Unknown command: %s\n", commandType.c_str());
    }
}

void WebSocketClient::sendAckResponse(const String& commandId, bool success) {
    StaticJsonDocument<200> doc;
    doc["type"] = "ack";
    doc["command_id"] = commandId;
    doc["success"] = success;
    doc["timestamp"] = millis();
    
    String message;
    serializeJson(doc, message);
    
    webSocket.sendTXT(message);
}

void WebSocketClient::updateStats() {
    uint32_t now = millis();
    if (now - lastStatsTime >= 1000) { // 每秒更新一次
        stats.avgSendRate = (float)blocksSentSinceLastStats * 1000.0f / (now - lastStatsTime);
        lastStatsTime = now;
        blocksSentSinceLastStats = 0;
    }
}

void WebSocketClient::handleConnectionRetry() {
    if (!serverConnected && wifiConnected) {
        uint32_t now = millis();
        if (now - lastConnectionAttempt >= connectionRetryInterval) {
            connect();
            Serial.printf("[WebSocketClient] Attempting to connect to server...\n");
        }
    }
}

void WebSocketClient::processSendQueue() {
    DataBlock* block = nullptr;
    
    while (xQueueReceive(sendQueue, &block, 0) == pdTRUE) {
        if (block && serverConnected && collectionActive) {
            String dataPacket = createDataPacket(block);
            
            if (webSocket.sendTXT(dataPacket)) {
                stats.totalBlocksSent++;
                stats.totalBytesSent += dataPacket.length();
                blocksSentSinceLastStats++;
            } else {
                stats.sendFailures++;
                Serial.printf("[WebSocketClient] ERROR: Failed to send data block\n");
            }
            
            // 释放数据块
            free(block);
        }
    }
    
    updateStats();
}
