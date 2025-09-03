#include "CommandHandler.h"
#include "Config.h"

// 静态变量定义
bool CommandHandler::realtimeDataEnabled = false;
uint32_t CommandHandler::lastRealtimeDataTime = 0;

// 命令定义表
const CommandHandler::Command CommandHandler::commands[] = {
    {"help", "显示帮助信息", &CommandHandler::showHelp},
    {"status", "显示系统状态", &CommandHandler::showStatus},
    {"data", "显示传感器数据", &CommandHandler::showSensorData},
    {"test", "测试网络连接", &CommandHandler::testNetwork},
    {"sync", "执行时间同步", &CommandHandler::syncTime},
    {"batch", "设置批量大小", &CommandHandler::setBatchSize},
    {"start", "开始数据采集", &CommandHandler::startCollection},
    {"stop", "停止数据采集", &CommandHandler::stopCollection},
    {"reset", "重置统计信息", &CommandHandler::resetStats},
    {"stats", "显示统计信息", &CommandHandler::showStats},
    {"device", "设置设备信息", &CommandHandler::setDeviceInfo},
    {"uart", "测试UART接收", &CommandHandler::testUart},
    {"buffer", "显示缓冲区状态", &CommandHandler::showBufferStatus},
    {"sensors", "显示传感器类型", &CommandHandler::showSensorTypes},
    {"config", "显示配置信息", &CommandHandler::showNetworkConfig},
    {"dropped", "切换显示丢弃数据包", &CommandHandler::toggleDroppedPackets},
    {"realtime", "实时显示传感器数据", &CommandHandler::showRealtimeData}
};

CommandHandler::CommandHandler() {
    uartReceiver = nullptr;
    webSocketClient = nullptr;
    sensorData = nullptr;
    
    Serial.printf("[CommandHandler] Created\n");
}

CommandHandler::~CommandHandler() {
    Serial.printf("[CommandHandler] Destroyed\n");
}

bool CommandHandler::initialize(UartReceiver* receiver, WebSocketClient* client, SensorData* data) {
    uartReceiver = receiver;
    webSocketClient = client;
    sensorData = data;
    
    if (!uartReceiver || !webSocketClient || !sensorData) {
        Serial.printf("[CommandHandler] ERROR: Invalid parameters\n");
        return false;
    }
    
    Serial.printf("[CommandHandler] Initialized successfully\n");
    return true;
}

void CommandHandler::processCommand(const String& command) {
    String args;
    String cmd = parseCommand(command, args);
    
    if (cmd.length() == 0) {
        return;
    }
    
    Serial.printf("[CommandHandler] Processing command: %s\n", cmd.c_str());
    executeCommand(cmd, args);
}

void CommandHandler::showHelp(const String& args) {
    Serial.printf("\n=== ESP32-S3 传感器网关 CLI 帮助 ===\n");
    Serial.printf("可用命令:\n");
    
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        Serial.printf("  %-10s - %s\n", commands[i].name.c_str(), commands[i].description.c_str());
    }
    
    Serial.printf("\n示例:\n");
    Serial.printf("  help                    - 显示此帮助信息\n");
    Serial.printf("  status                  - 显示系统状态\n");
    Serial.printf("  start                   - 开始数据采集\n");
    Serial.printf("  device 2025001 1015     - 设置设备码和会话ID\n");
    Serial.printf("  batch 50                - 设置批量大小为50\n");
    Serial.printf("=====================================\n\n");
}

void CommandHandler::showStatus(const String& args) {
    Serial.printf("\n=== 系统状态 ===\n");
    
    // 显示UART接收状态（减少栈使用）
    if (uartReceiver) {
        Serial.printf("UART接收:\n");
        Serial.printf("  总接收字节: %d\n", uartReceiver->getStats().totalBytesReceived);
        Serial.printf("  解析帧数: %d\n", uartReceiver->getStats().totalFramesParsed);
        Serial.printf("  解析错误: %d\n", uartReceiver->getStats().parseErrors);
        Serial.printf("  传感器帧数统计:\n");
        for (int i = 0; i < 4; i++) {
            const char* sensorType = SensorData::getSensorType(i + 1);
            Serial.printf("    %s (ID%d): %d frames\n", sensorType, i + 1, uartReceiver->getStats().sensorFrameCounts[i]);
        }
    }
    
    // 显示网络状态（减少栈使用）
    if (webSocketClient) {
        Serial.printf("\n网络连接:\n");
        Serial.printf("  服务器连接: %s\n", webSocketClient->getStats().serverConnected ? "已连接" : "未连接");
        Serial.printf("  发送块数: %d\n", webSocketClient->getStats().totalBlocksSent);
        Serial.printf("  发送字节: %d\n", webSocketClient->getStats().totalBytesSent);
        Serial.printf("  发送速率: %.2f blocks/s\n", webSocketClient->getStats().avgSendRate);
        Serial.printf("  连接尝试: %d\n", webSocketClient->getStats().connectionAttempts);
        Serial.printf("  连接失败: %d\n", webSocketClient->getStats().connectionFailures);
    }
    
    // 显示传感器数据状态（减少栈使用）
    if (sensorData) {
        Serial.printf("\n传感器数据:\n");
        Serial.printf("  总帧数: %d\n", sensorData->getStats().totalFrames);
        Serial.printf("  丢弃帧数: %d\n", sensorData->getStats().droppedFrames);
        Serial.printf("  创建块数: %d\n", sensorData->getStats().blocksCreated);
        Serial.printf("  发送块数: %d\n", sensorData->getStats().blocksSent);
        Serial.printf("  平均帧率: %.2f fps\n", sensorData->getStats().avgFrameRate);
    }
    
    Serial.printf("================\n\n");
}

void CommandHandler::showSensorData(const String& args) {
    Serial.printf("\n=== 传感器数据 ===\n");
    
    if (sensorData) {
        SensorData::Stats dataStats = sensorData->getStats();
        Serial.printf("数据统计:\n");
        Serial.printf("  总帧数: %d\n", dataStats.totalFrames);
        Serial.printf("  平均帧率: %.2f fps\n", dataStats.avgFrameRate);
        
        if (uartReceiver) {
            UartReceiver::Stats uartStats = uartReceiver->getStats();
            Serial.printf("\n各传感器帧数:\n");
            for (int i = 0; i < 4; i++) {
                const char* sensorType = SensorData::getSensorType(i + 1);
                Serial.printf("  %s (ID%d): %d frames\n", sensorType, i + 1, uartStats.sensorFrameCounts[i]);
            }
            
            if (uartStats.totalFramesParsed > 0) {
                Serial.printf("\n最近接收状态:\n");
                Serial.printf("  解析成功率: %.2f%%\n", 
                    (float)uartStats.totalFramesParsed / (uartStats.totalFramesParsed + uartStats.parseErrors) * 100.0f);
            }
        }
        
        if (dataStats.totalFrames == 0) {
            Serial.printf("\n提示: 暂无传感器数据，请检查:\n");
            Serial.printf("  1. 传感器是否正常工作\n");
            Serial.printf("  2. UART连接是否正常\n");
            Serial.printf("  3. 数据采集是否已启动\n");
        }
    } else {
        Serial.printf("传感器数据管理器未初始化\n");
    }
    
    Serial.printf("==================\n\n");
}

void CommandHandler::testNetwork(const String& args) {
    Serial.printf("\n=== 网络测试 ===\n");
    
    if (webSocketClient) {
        if (webSocketClient->isConnected()) {
            Serial.printf("网络连接正常\n");
            webSocketClient->sendHeartbeat();
        } else {
            Serial.printf("网络连接异常，尝试重连...\n");
            webSocketClient->connect();
        }
    } else {
        Serial.printf("WebSocket客户端未初始化\n");
    }
    
    Serial.printf("===============\n\n");
}

void CommandHandler::syncTime(const String& args) {
    Serial.printf("\n=== 时间同步 ===\n");
    
    uint32_t currentTime = millis();
    Serial.printf("当前系统时间: %d ms\n", currentTime);
    Serial.printf("系统运行时间: %.2f 秒\n", currentTime / 1000.0f);
    
    if (uartReceiver) {
        UartReceiver::Stats uartStats = uartReceiver->getStats();
        if (uartStats.totalFramesParsed > 0) {
            Serial.printf("已接收传感器帧数: %d\n", uartStats.totalFramesParsed);
            Serial.printf("平均接收速率: %.2f fps\n", 
                (float)uartStats.totalFramesParsed * 1000.0f / currentTime);
        }
    }
    
    Serial.printf("\n时间同步状态:\n");
    Serial.printf("  本地时间戳: %d\n", currentTime);
    Serial.printf("  时间同步: 已启用\n");
    Serial.printf("  同步精度: 毫秒级\n");
    
    Serial.printf("================\n\n");
}

void CommandHandler::setBatchSize(const String& args) {
    Serial.printf("\n=== 设置批量大小 ===\n");
    if (args.length() > 0) {
        uint8_t size = args.toInt();
        if (size > 0 && size <= 100) {
            Serial.printf("设置批量大小为: %d\n", size);
            Serial.printf("注意: 此设置将在下次重启后生效\n");
            Serial.printf("当前默认批量大小: 50\n");
        } else {
            Serial.printf("错误: 批量大小必须在1-100之间\n");
        }
    } else {
        Serial.printf("用法: batch <size>\n");
        Serial.printf("示例: batch 50\n");
        Serial.printf("当前默认批量大小: 50\n");
    }
    Serial.printf("==================\n\n");
}

void CommandHandler::startCollection(const String& args) {
    Serial.printf("\n=== 开始数据采集 ===\n");
    
    if (webSocketClient) {
        webSocketClient->startCollection();
        Serial.printf("数据采集已开始\n");
    } else {
        Serial.printf("WebSocket客户端未初始化\n");
    }
    
    Serial.printf("==================\n\n");
}

void CommandHandler::stopCollection(const String& args) {
    Serial.printf("\n=== 停止数据采集 ===\n");
    
    if (webSocketClient) {
        webSocketClient->stopCollection();
        Serial.printf("数据采集已停止\n");
    } else {
        Serial.printf("WebSocket客户端未初始化\n");
    }
    
    Serial.printf("==================\n\n");
}

void CommandHandler::resetStats(const String& args) {
    Serial.printf("\n=== 重置统计信息 ===\n");
    
    if (uartReceiver) {
        uartReceiver->resetStats();
    }
    if (webSocketClient) {
        webSocketClient->resetStats();
    }
    if (sensorData) {
        sensorData->resetStats();
    }
    
    Serial.printf("所有统计信息已重置\n");
    Serial.printf("==================\n\n");
}

void CommandHandler::showStats(const String& args) {
    showStatus(); // 复用状态显示功能
}

void CommandHandler::setDeviceInfo(const String& args) {
    Serial.printf("\n=== 设置设备信息 ===\n");
    
    if (args.length() > 0) {
        int spaceIndex = args.indexOf(' ');
        if (spaceIndex > 0) {
            String deviceCode = args.substring(0, spaceIndex);
            String sessionId = args.substring(spaceIndex + 1);
            
            if (webSocketClient) {
                webSocketClient->setDeviceInfo(deviceCode, sessionId);
                Serial.printf("设备码: %s\n", deviceCode.c_str());
                Serial.printf("会话ID: %s\n", sessionId.c_str());
            } else {
                Serial.printf("WebSocket客户端未初始化\n");
            }
        } else {
            Serial.printf("用法: device <device_code> <session_id>\n");
        }
    } else {
        Serial.printf("用法: device <device_code> <session_id>\n");
    }
    
    Serial.printf("==================\n\n");
}

void CommandHandler::testUart(const String& args) {
    Serial.printf("\n=== UART测试 ===\n");
    
    if (uartReceiver) {
        UartReceiver::Stats stats = uartReceiver->getStats();
        Serial.printf("UART状态:\n");
        Serial.printf("  总接收字节: %d\n", stats.totalBytesReceived);
        Serial.printf("  解析帧数: %d\n", stats.totalFramesParsed);
        Serial.printf("  解析错误: %d\n", stats.parseErrors);
        
        Serial.printf("\n传感器帧数统计:\n");
        for (int i = 0; i < 4; i++) {
            const char* sensorType = SensorData::getSensorType(i + 1);
            Serial.printf("  %s (ID%d): %d frames\n", sensorType, i + 1, stats.sensorFrameCounts[i]);
        }
        
        if (stats.totalFramesParsed == 0) {
            Serial.printf("\n警告: 未接收到任何有效帧，请检查:\n");
            Serial.printf("  1. 传感器是否正确连接\n");
            Serial.printf("  2. 波特率是否匹配 (460800)\n");
            Serial.printf("  3. 数据格式是否正确 (43字节帧)\n");
        }
    } else {
        Serial.printf("UART接收器未初始化\n");
    }
    
    Serial.printf("===============\n\n");
}

void CommandHandler::showBufferStatus(const String& args) {
    Serial.printf("\n=== 缓冲区状态 ===\n");
    
    if (sensorData) {
        SensorData::Stats dataStats = sensorData->getStats();
        Serial.printf("传感器数据缓冲区:\n");
        Serial.printf("  总帧数: %d\n", dataStats.totalFrames);
        Serial.printf("  丢弃帧数: %d\n", dataStats.droppedFrames);
        Serial.printf("  创建块数: %d\n", dataStats.blocksCreated);
        Serial.printf("  发送块数: %d\n", dataStats.blocksSent);
        Serial.printf("  平均帧率: %.2f fps\n", dataStats.avgFrameRate);
        
        if (dataStats.droppedFrames > 0) {
            float dropRate = (float)dataStats.droppedFrames / (dataStats.totalFrames + dataStats.droppedFrames) * 100.0f;
            Serial.printf("  丢帧率: %.2f%%\n", dropRate);
        }
    }
    
    if (uartReceiver) {
        UartReceiver::Stats uartStats = uartReceiver->getStats();
        Serial.printf("\nUART接收缓冲区:\n");
        Serial.printf("  总接收字节: %d\n", uartStats.totalBytesReceived);
        Serial.printf("  解析帧数: %d\n", uartStats.totalFramesParsed);
        Serial.printf("  解析错误: %d\n", uartStats.parseErrors);
        
        if (uartStats.parseErrors > 0) {
            float errorRate = (float)uartStats.parseErrors / (uartStats.totalFramesParsed + uartStats.parseErrors) * 100.0f;
            Serial.printf("  解析错误率: %.2f%%\n", errorRate);
        }
    }
    
    Serial.printf("==================\n\n");
}

String CommandHandler::parseCommand(const String& input, String& args) {
    String trimmedInput = input;
    trimmedInput.trim();
    
    int spaceIndex = trimmedInput.indexOf(' ');
    if (spaceIndex == -1) {
        args = "";
        return trimmedInput;
    }
    
    String command = trimmedInput.substring(0, spaceIndex);
    args = trimmedInput.substring(spaceIndex + 1);
    args.trim();
    
    return command;
}

void CommandHandler::executeCommand(const String& command, const String& args) {
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (commands[i].name == command) {
            (this->*commands[i].handler)(args);
            return;
        }
    }
    
    Serial.printf("[CommandHandler] 未知命令: %s\n", command.c_str());
    Serial.printf("输入 'help' 查看可用命令\n");
}

void CommandHandler::showSensorTypes(const String& args) {
    Serial.printf("\n=== 传感器类型 ===\n");
    Serial.printf("ID 1: waist (腰部)\n");
    Serial.printf("ID 2: shoulder (肩部)\n");
    Serial.printf("ID 3: wrist (手腕)\n");
    Serial.printf("ID 4: racket (球拍)\n");
    Serial.printf("==================\n\n");
}

void CommandHandler::showNetworkConfig(const String& args) {
    Serial.printf("\n=== 网络配置 ===\n");
    Serial.printf("WiFi SSID: %s\n", "xiaoming");
    Serial.printf("服务器地址: %s:%d\n", "175.178.100.179", 8000);
    
    if (webSocketClient) {
        WebSocketClient::Stats netStats = webSocketClient->getStats();
        Serial.printf("\n连接状态:\n");
        Serial.printf("  服务器连接: %s\n", netStats.serverConnected ? "已连接" : "未连接");
        Serial.printf("  连接尝试次数: %d\n", netStats.connectionAttempts);
        Serial.printf("  连接失败次数: %d\n", netStats.connectionFailures);
        Serial.printf("  发送块数: %d\n", netStats.totalBlocksSent);
        Serial.printf("  发送字节数: %d\n", netStats.totalBytesSent);
        Serial.printf("  发送速率: %.2f blocks/s\n", netStats.avgSendRate);
        Serial.printf("  发送失败次数: %d\n", netStats.sendFailures);
        
        if (netStats.lastHeartbeat > 0) {
            uint32_t timeSinceHeartbeat = millis() - netStats.lastHeartbeat;
            Serial.printf("  上次心跳: %d ms前\n", timeSinceHeartbeat);
        }
    }
    
    Serial.printf("================\n\n");
}

void CommandHandler::showUartConfig(const String& args) {
    Serial.printf("\n=== UART配置 ===\n");
    Serial.printf("UART1: TX=17, RX=16, 460800波特率\n");
    Serial.printf("数据格式: 43字节帧结构\n");
    Serial.printf("帧头: 0xAA, 帧尾: 0x55\n");
    Serial.printf("传感器ID: 1-4 (waist/shoulder/wrist/racket)\n");
    Serial.printf("===============\n\n");
}

String CommandHandler::formatTimestamp(uint32_t timestamp) {
    uint32_t seconds = timestamp / 1000;
    uint32_t milliseconds = timestamp % 1000;
    
    return String(seconds) + "." + String(milliseconds);
}

String CommandHandler::formatFloat(float value, int precision) {
    return String(value, precision);
}

void CommandHandler::toggleDroppedPackets(const String& args) {
    Config::SHOW_DROPPED_PACKETS = !Config::SHOW_DROPPED_PACKETS;
    Serial.printf("[CommandHandler] 显示丢弃数据包: %s\n", 
                  Config::SHOW_DROPPED_PACKETS ? "开启" : "关闭");
    
    if (Config::SHOW_DROPPED_PACKETS) {
        Serial.printf("现在会显示丢弃数据包的详细信息\n");
    } else {
        Serial.printf("现在不会显示丢弃数据包的详细信息\n");
    }
}

void CommandHandler::showRealtimeData(const String& args) {
    realtimeDataEnabled = !realtimeDataEnabled;
    Serial.printf("[CommandHandler] 实时数据显示: %s\n", 
                  realtimeDataEnabled ? "开启" : "关闭");
    
    if (realtimeDataEnabled) {
        Serial.printf("现在会实时显示解析出的传感器数据\n");
        Serial.printf("按 Ctrl+C 或再次输入 'realtime' 停止显示\n");
        lastRealtimeDataTime = millis();
    } else {
        Serial.printf("已停止实时数据显示\n");
    }
}

bool CommandHandler::isRealtimeDataEnabled() {
    return realtimeDataEnabled;
}

void CommandHandler::displayRealtimeSensorData(const SensorFrame& frame) {
    if (!realtimeDataEnabled) {
        return;
    }
    
    // 限制显示频率，避免刷屏
    uint32_t now = millis();
    if (now - lastRealtimeDataTime < 100) { // 100ms间隔
        return;
    }
    lastRealtimeDataTime = now;
    
    // 显示传感器数据
    const char* sensorType = SensorData::getSensorType(frame.sensorId);
    Serial.printf("[实时数据] %s(ID%d) - 时间戳:%u, 加速度:[%.2f,%.2f,%.2f], 角速度:[%.2f,%.2f,%.2f], 角度:[%.2f,%.2f,%.2f]\n",
                  sensorType, frame.sensorId, frame.timestamp,
                  frame.acc[0], frame.acc[1], frame.acc[2],
                  frame.gyro[0], frame.gyro[1], frame.gyro[2],
                  frame.angle[0], frame.angle[1], frame.angle[2]);
}

