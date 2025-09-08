#include "WebSocketClient.h"
#include <ArduinoJson.h>
#include "BufferPool.h"
#include "Config.h"
#include "CommandHandler.h"

// 全局变量，用于静态回调函数访问实例
static WebSocketClient* g_webSocketClientInstance = nullptr;
uint32_t WebSocketClient::lastSendPrintTime = 0;

WebSocketClient::WebSocketClient() {
    serverPort = 8080;
    collectionActive = false;
    uploadCompletePending = false;
    wifiConnected = false;
    serverConnected = false;
    lastConnectionAttempt = 0;
    connectionRetryInterval = 5000; // 5秒重试间隔
    
    mutex = xSemaphoreCreateMutex();
    sendQueue = xQueueCreate(MAX_QUEUE_SIZE, sizeof(DataBlock*));
    bufferPool = nullptr;
    commandHandler = nullptr;
    
    memset(&stats, 0, sizeof(stats));
    lastStatsTime = millis();
    blocksSentSinceLastStats = 0;
    
    // 设置全局实例指针
    g_webSocketClientInstance = this;
    
    Serial.printf("[WebSocketClient] Created\n");
}

WebSocketClient::~WebSocketClient() {
    disconnect();
    
    // 清除全局实例指针
    if (g_webSocketClientInstance == this) {
        g_webSocketClientInstance = nullptr;
    }
    
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    
    if (sendQueue) {
        vQueueDelete(sendQueue);
    }
}

bool WebSocketClient::initialize(const char* ssid, const char* password, const char* url, uint16_t port, const char* deviceCode) {
    serverUrl = String(url);
    serverPort = port;
    this->deviceCode = String(deviceCode);
    
    // 构建WebSocket路径: /ws/esp32/{device_code}/
    String wsPath = "/ws/esp32/";
    wsPath += deviceCode;
    wsPath += "/";
    
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
    webSocket.begin(serverUrl.c_str(), serverPort, wsPath.c_str());
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
    
    Serial.printf("[WebSocketClient] Initialized. Server: %s:%d%s\n", serverUrl.c_str(), serverPort, wsPath.c_str());
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
        
        // 构建WebSocket路径: /ws/esp32/{device_code}/
        String wsPath = "/ws/esp32/";
        wsPath += deviceCode;
        wsPath += "/";
        
        webSocket.begin(serverUrl.c_str(), serverPort, wsPath.c_str());
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
    
    Serial.printf("[WebSocketClient] Stop connected\n");
}

bool WebSocketClient::sendDataBlock(DataBlock* block) {
    // 详细的状态检查
    if (!block) {
        
        Serial.printf("[WebSocketClient] Error: sendDataBlock failed - block is null\n");
        return false;
    }
    
    // 检查连接状态
    if (!serverConnected) {
        if(Config::DEBUG_PPRINT)
        Serial.printf("[WebSocketClient] DEBUG: sendDataBlock failed - serverConnected=%d\n", 
                     serverConnected);
        return false;
    }
    
    if (!collectionActive) {
        if(Config::DEBUG_PPRINT)
        Serial.printf("[WebSocketClient] DEBUG: sendDataBlock failed - collection not active (collectionActive=%d)\n", 
                     collectionActive);
        return false;
    }
    
    // 将数据块加入发送队列
    if (xQueueSend(sendQueue, &block, 0) != pdTRUE) {
        Serial.printf("[WebSocketClient] WARNING: Send queue full, dropping block\n");
        return false;
    }
    
    // 成功添加到队列
    if(Config::DEBUG_PPRINT){
        Serial.printf("[WebSocketClient] Data block added to send queue successfully\n");
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
    Stats currentStats = stats;
    currentStats.serverConnected = serverConnected;  // 更新当前连接状态
    return currentStats;
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
    uploadCompletePending = true;  // 标记需要发送upload_complete消息
    Serial.printf("[WebSocketClient] Data collection stopped, upload_complete pending\n");
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
    
    // 定期输出连接状态（每10秒一次，用于调试）
    static uint32_t lastStatusTime = 0;
    uint32_t now = millis();
    if (now - lastStatusTime > 10000) {
        Serial.printf("[WebSocketClient] Status check - serverConnected: %d, wifiConnected: %d, collectionActive: %d\n", 
                     serverConnected, wifiConnected, collectionActive);
        lastStatusTime = now;
    }
}

String WebSocketClient::createDataPacket(DataBlock* block) {
    // 安全检查
    if (!block) {
        Serial.printf("[WebSocketClient] ERROR: Block is null\n");
        return "";
    }
    
    if (block->frameCount == 0) {
        Serial.printf("[WebSocketClient] ERROR: Block has no frames\n");
        return "";
    }
    
    if (block->frameCount > DataBlock::MAX_FRAMES) {
        Serial.printf("[WebSocketClient] ERROR: Block has too many frames: %d\n", block->frameCount);
        return "";
    }
    
    // 使用动态分配避免栈溢出，减小大小以提高稳定性
    DynamicJsonDocument doc(8192);  // 从8192减小到6144
    
    // 按照新格式组织数据包
    doc["type"] = Config::SENSOR_DATA_PACKET_TYPE;
    doc["device_code"] = deviceCode;
    doc["sensor_type"] = SensorData::getSensorType(block->frames[0].sensorId);
    doc["timestamp"] = millis(); // 使用当前时间戳
    
    // 创建数据数组
    JsonArray data = doc.createNestedArray("data");
    if(Config::DEBUG_PPRINT){
        Serial.printf("[WebSocketClient]  Creating data packet with %d frames (expected: 30)\n", block->frameCount);
    }
    
    
    if (block->frameCount < 30) {
        Serial.printf("[WebSocketClient] WARNING: Data block has only %d frames, expected 30!\n", block->frameCount);
    }
    
    int successfulFrames = 0;
    int framesToProcess = block->frameCount;  // 现在直接使用所有帧，因为MAX_FRAMES已经是30
    for (int i = 0; i < framesToProcess; i++) {
        // 检查数组边界
        if (i >= DataBlock::MAX_FRAMES) {
            Serial.printf("[WebSocketClient] ERROR: Frame index %d exceeds MAX_FRAMES %d\n", i, DataBlock::MAX_FRAMES);
            break;
        }
        
        JsonObject frame = data.createNestedObject();
        if (frame.isNull()) {
            Serial.printf("[WebSocketClient] ERROR: Failed to create JSON object for frame %d, skipping this frame\n", i);
            continue;  // 跳过这一帧，继续处理下一帧
        }
        
        // 检查数据有效性
        bool validData = true;
        for (int j = 0; j < 3; j++) {
            if (isnan(block->frames[i].acc[j]) || isinf(block->frames[i].acc[j])) {
                validData = false;
                Serial.printf("[WebSocketClient] WARNING: Invalid acc[%d] data at frame %d: %f\n", j, i, block->frames[i].acc[j]);
            }
        }
        
        // 加速度数据
        JsonArray acc = frame.createNestedArray("acc");
        if (acc.isNull()) {
            Serial.printf("[WebSocketClient] ERROR: Failed to create acc array for frame %d, skipping this frame\n", i);
            continue;
        }
        
        if (validData) {
            acc.add(block->frames[i].acc[0]);
            acc.add(block->frames[i].acc[1]);
            acc.add(block->frames[i].acc[2]);
        } else {
            // 如果数据无效，使用默认值
            acc.add(0.0);
            acc.add(0.0);
            acc.add(0.0);
            Serial.printf("[WebSocketClient] WARNING: Using default acc values for frame %d\n", i);
        }
        
        // 角速度数据
        JsonArray gyro = frame.createNestedArray("gyro");
        if (gyro.isNull()) {
            Serial.printf("[WebSocketClient] ERROR: Failed to create gyro array for frame %d, skipping this frame\n", i);
            continue;
        }
        gyro.add(block->frames[i].gyro[0]);
        gyro.add(block->frames[i].gyro[1]);
        gyro.add(block->frames[i].gyro[2]);
        
        // 角度数据
        JsonArray angle = frame.createNestedArray("angle");
        if (angle.isNull()) {
            Serial.printf("[WebSocketClient] ERROR: Failed to create angle array for frame %d, skipping this frame\n", i);
            continue;
        }
        angle.add(block->frames[i].angle[0]);
        angle.add(block->frames[i].angle[1]);
        angle.add(block->frames[i].angle[2]);
        
        // 传感器ID
        frame["sensor_id"] = block->frames[i].sensorId;
        
        // 时间戳（使用原始时间戳，避免精度丢失）
        frame["timestamp"] = block->frames[i].timestamp;
        
        // 成功处理了一帧
        successfulFrames++;
        
        // 调试信息：检查最后一个帧
        if (i == block->frameCount - 1) {
            if(Config::DEBUG_PPRINT){
                Serial.printf("[WebSocketClient] DEBUG: Last frame %d - acc: [%f, %f, %f], gyro: [%f, %f, %f]\n", 
                            i, block->frames[i].acc[0], block->frames[i].acc[1], block->frames[i].acc[2],
                            block->frames[i].gyro[0], block->frames[i].gyro[1], block->frames[i].gyro[2]);
            }
        }
    }
    
    if(Config::DEBUG_PPRINT){
        Serial.printf("[WebSocketClient] DEBUG: Successfully processed %d out of %d frames\n", successfulFrames, block->frameCount);
    }
   
    
    // 添加会话ID（如果存在）
    if (sessionId.length() > 0) {
        doc["session_id"] = sessionId;
    }

    size_t docSize = measureJson(doc);
    // 检查JSON文档大小
    if(Config::DEBUG_PPRINT){
     
        Serial.printf("[WebSocketClient] DEBUG: JSON document size: %d bytes, max capacity: %d bytes\n", 
                    docSize, doc.capacity());
    }

    
    if (docSize >= doc.capacity()) {
        Serial.printf("[WebSocketClient] ERROR: JSON document too large! Size: %d, Capacity: %d\n", 
                     docSize, doc.capacity());
        return "";
    }
    
    String result;
    size_t bytesWritten = serializeJson(doc, result);
    
    if (bytesWritten == 0) {
        Serial.printf("[WebSocketClient] ERROR: Failed to serialize JSON document\n");
        return "";
    }
    if(Config::DEBUG_PPRINT){
        Serial.printf("[WebSocketClient] DEBUG: JSON serialized size: %d bytes\n", bytesWritten);
    }
    
    
    // 验证序列化结果
    if (bytesWritten != docSize) {
        Serial.printf("[WebSocketClient] WARNING: Serialized size (%d) != measured size (%d)\n", 
                     bytesWritten, docSize);
    }
    
    return result;
}

void WebSocketClient::webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    // 通过全局实例指针访问WebSocketClient实例
    if(Config::DEBUG_PPRINT){
        Serial.printf("[WebSocketClient] webSocketEvent called with type: %d, length: %d\n", type, length);
    }

    
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WebSocketClient] Disconnected from server\n");
            if (g_webSocketClientInstance) {
                g_webSocketClientInstance->serverConnected = false;
                Serial.printf("[WebSocketClient] serverConnected set to false\n");
            }
            break;
            
        case WStype_CONNECTED:
            Serial.printf("[WebSocketClient] Connected to server successfully\n");
            if (g_webSocketClientInstance) {
                g_webSocketClientInstance->serverConnected = true;
                Serial.printf("[WebSocketClient] serverConnected set to true\n");
            }
            break;
            
        case WStype_TEXT:
            if (Config::DEBUG_PPRINT)
            {
                Serial.printf("[WebSocketClient] Received text message of length %d\n", length);
                Serial.printf("[WebSocketClient] Received: %s\n", payload);
            }
            
            
            // 处理服务器命令
            if (g_webSocketClientInstance) {
                String command = String((char*)payload);
                g_webSocketClientInstance->handleServerCommand(command);
            }
            break;
            
        case WStype_ERROR:
            Serial.printf("[WebSocketClient] WebSocket error occurred\n");
            if (g_webSocketClientInstance) {
                g_webSocketClientInstance->serverConnected = false;
                Serial.printf("[WebSocketClient] serverConnected set to false due to error\n");
            }
            break;
            
        default:
            Serial.printf("[WebSocketClient] Unknown WebSocket event: %d\n", type);
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
    String commandId = doc["command_id"] | doc["id"] | "";
    bool success = false;
    if(Config::DEBUG_PPRINT){
        Serial.printf("[WebSocketClient] DEBUG: Parsed command type: %s, command ID: %s\n", commandType.c_str(), commandId.c_str());
    }
  
    
    if (commandType == "start_collection") {
        deviceCode = doc["device_code"] | "";
        // 支持数字和字符串类型的session_id
        if (doc.containsKey("session_id")) {
            if (doc["session_id"].is<int>()) {
                sessionId = String(doc["session_id"].as<int>());
            } else if (doc["session_id"].is<String>()) {
                sessionId = doc["session_id"].as<String>();
            } else {
                sessionId = "";
            }
        }
        startCollection();
        success = true;
        Serial.printf("[WebSocketClient] Start collection command executed successfully\n");
        
    } else if (commandType == "stop_collection") {
        // 保存sessionId和deviceCode（如果命令中包含的话）
        if (doc.containsKey("session_id")) {
            // 支持数字和字符串类型的session_id
            if (doc["session_id"].is<int>()) {
                sessionId = String(doc["session_id"].as<int>());
            } else if (doc["session_id"].is<String>()) {
                sessionId = doc["session_id"].as<String>();
            } else {
                sessionId = "";
            }
            Serial.printf("[WebSocketClient] DEBUG: Saved sessionId: '%s'\n", sessionId.c_str());
        }
        if (doc.containsKey("device_code")) {
            deviceCode = doc["device_code"] | "";
            Serial.printf("[WebSocketClient] DEBUG: Saved deviceCode: '%s'\n", deviceCode.c_str());
        }
        stopCollection();
        success = true;
        Serial.printf("[WebSocketClient] Stop collection command executed successfully\n");
        
    } else if (commandType == "sync" || commandType == "SYNC") {
        // 处理时间同步命令
        Serial.printf("[WebSocketClient] Time sync command received from server\n");
        if (commandHandler) {
            // 调用CommandHandler的sync命令
            commandHandler->processCommand("sync");
            success = true;
            Serial.printf("[WebSocketClient] Time sync command executed successfully\n");
        } else {
            Serial.printf("[WebSocketClient] ERROR: CommandHandler not available\n");
            success = false;
        }
        
    } else if (commandType == "set_batch" || commandType == "SET_BATCH") {
        // 处理批量大小设置命令
        if (doc.containsKey("batch_size")) {
            uint32_t batchSize = doc["batch_size"];
            // 这里可以调用配置模块来设置批量大小
            Serial.printf("[WebSocketClient] Set batch size command received: %u\n", batchSize);
            success = true;
        } else {
            Serial.printf("[WebSocketClient] ERROR: Set batch command missing batch_size\n");
        }
        
    } else if (commandType == "get_status" || commandType == "GET_STATUS") {
        // 处理状态查询命令
        sendStatusResponse(commandId);
        success = true;
        Serial.printf("[WebSocketClient] Status query command processed\n");
        
    } else if (commandType == "heartbeat" || commandType == "HEARTBEAT") {
        // 处理心跳命令
        sendHeartbeat();
        success = true;
        Serial.printf("[WebSocketClient] Heartbeat command processed\n");
        
    } else if (commandType == "batch_sensor_data_response" ) {
       
        if(Config::DEBUG_PPRINT){
            Serial.printf("[WebSocketClient] DEBUG: Received batch_sensor_data_response: %s\n", jsonCommand.c_str());
        }
        success = true;
    }else {
        Serial.printf("[WebSocketClient] Unknown command: %s\n", commandType.c_str());
        success = false;
    }
    
    // 发送ACK响应（如果有command_id）
    if (commandId.length() > 0) {
        sendAckResponse(commandId, success);
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

void WebSocketClient::sendStatusResponse(const String& commandId) {
    StaticJsonDocument<512> doc;
    doc["type"] = "status_response";
    doc["command_id"] = commandId;
    doc["timestamp"] = millis();
    
    // 连接状态
    doc["connection"]["wifi_connected"] = wifiConnected;
    doc["connection"]["server_connected"] = serverConnected;
    doc["connection"]["collection_active"] = collectionActive;
    
    // 设备信息
    doc["device"]["device_code"] = deviceCode;
    doc["device"]["session_id"] = sessionId;
    doc["device"]["firmware_version"] = "V3.3";
    
    // 统计信息
    doc["stats"]["total_blocks_sent"] = stats.totalBlocksSent;
    doc["stats"]["total_bytes_sent"] = stats.totalBytesSent;
    doc["stats"]["send_failures"] = stats.sendFailures;
    doc["stats"]["avg_send_rate"] = stats.avgSendRate;
    doc["stats"]["connection_attempts"] = stats.connectionAttempts;
    
    // 系统信息
    doc["system"]["free_heap"] = ESP.getFreeHeap();
    doc["system"]["uptime"] = millis();
    
    String message;
    serializeJson(doc, message);
    
    webSocket.sendTXT(message);
    Serial.printf("[WebSocketClient] Status response sent\n");
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

void WebSocketClient::setBufferPool(BufferPool* bufferPoolInstance) {
    bufferPool = bufferPoolInstance;
    Serial.printf("[WebSocketClient] BufferPool set\n");
}

void WebSocketClient::setCommandHandler(CommandHandler* commandHandlerInstance) {
    commandHandler = commandHandlerInstance;
    Serial.printf("[WebSocketClient] CommandHandler set\n");
}

void WebSocketClient::setConnectionStatus(bool connected) {
    if (serverConnected != connected) {
        bool oldState = serverConnected;
        serverConnected = connected;
        Serial.printf("[WebSocketClient] Connection status manually set: %d -> %d\n", oldState, serverConnected);
    }
}

void WebSocketClient::processSendQueue() {
    DataBlock* block = nullptr;
    
    while (xQueueReceive(sendQueue, &block, 0) == pdTRUE) {
        if(Config::DEBUG_PPRINT){
            Serial.printf("[WebSocketClient] DEBUG: Processing block from queue - block: %p, serverConnected: %d, collectionActive: %d\n", 
                        block, serverConnected, collectionActive);
        }
        if (block && serverConnected && collectionActive) {
            String dataPacket = createDataPacket(block);
       
            bool sendResult = webSocket.sendTXT(dataPacket);
            if(Config::DEBUG_PPRINT){
                Serial.printf("[WebSocketClient] DEBUG: Created data packet, length: %d bytes\n", dataPacket.length());
                Serial.printf("[WebSocketClient] DEBUG: sendTXT result: %d\n", sendResult);
            }
            
            
            if (sendResult) {
                stats.totalBlocksSent++;
                stats.totalBytesSent += dataPacket.length();
                blocksSentSinceLastStats++;

                uint32_t now = millis();

                if(now - lastSendPrintTime > 2000){ // 每2秒打印一次
                    Serial.printf("[WebSocketClient] Sent block %d, size: %d bytes,sendSinceLastStats: %d,sendQueueSize: %d\n", 
                                stats.totalBlocksSent, stats.totalBytesSent, blocksSentSinceLastStats++, uxQueueMessagesWaiting(sendQueue));
                    lastSendPrintTime = now;
                }
            } else {
                stats.sendFailures++;
                    Serial.printf("[WebSocketClient] ERROR: Failed to send data block - WebSocket sendTXT returned false\n");

            }
        } else {
            Serial.printf("[WebSocketClient] WARNING: Block not sent - block: %p, serverConnected: %d, collectionActive: %d\n", 
                         block, serverConnected, collectionActive);
        }
        
        // 无论发送成功与否，都需要释放数据块
        if (block) {
            if (bufferPool) {
                // 使用BufferPool正确释放数据块
                bufferPool->releaseBlock(block);
                if(Config::DEBUG_PPRINT){
                    Serial.printf("[WebSocketClient] DEBUG: Block released to BufferPool\n");
                }
            } else {
                // 如果没有BufferPool，直接释放（不推荐）
                free(block);
                Serial.printf("[WebSocketClient] Warning: Block freed directly\n");
            }
        }
    }
    
    // 检查是否需要发送upload_complete消息
    if (uploadCompletePending && uxQueueMessagesWaiting(sendQueue) == 0) {
        Serial.printf("[WebSocketClient] Send queue is empty, sending upload_complete message\n");
        sendUploadComplete();
    }
    
    updateStats();
}

void WebSocketClient::sendUploadComplete() {
    if (!serverConnected) {
        Serial.printf("[WebSocketClient] ERROR:Cannot send upload_complete - server not connected\n");
        return;
    }
    if(Config::DEBUG_PPRINT){
        Serial.printf("[WebSocketClient] DEBUG: sendUploadComplete - sessionId: '%s' (len=%d), deviceCode: '%s' (len=%d)\n", 
                    sessionId.c_str(), sessionId.length(), deviceCode.c_str(), deviceCode.length());
    }
    if (sessionId.length() == 0 || deviceCode.length() == 0) {
        Serial.printf("[WebSocketClient] ERROR:Cannot send upload_complete - missing sessionId or deviceCode\n");
        return;
    }
    
    StaticJsonDocument<256> doc;
    doc["type"] = "upload_complete";
    doc["session_id"] = sessionId;
    doc["device_code"] = deviceCode;
    doc["timestamp"] = millis();
    
    String message;
    serializeJson(doc, message);
    
    bool sendResult = webSocket.sendTXT(message);
    if (sendResult) {
        Serial.printf("[WebSocketClient] Upload complete message sent successfully\n");
        uploadCompletePending = false;  // 重置标志
    } else {
        Serial.printf("[WebSocketClient] ERROR: Failed to send upload_complete message\n");
    }
}
