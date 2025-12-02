#include "CommandHandler.h"
#include "Config.h"
#include "BluetoothConfig.h"

// 静态变量定义
bool CommandHandler::realtimeDataEnabled = false;
uint32_t CommandHandler::lastRealtimeDataTime = 0;

// 命令定义表
const CommandHandler::Command CommandHandler::commands[] = {
    {"help", "显示帮助信息", &CommandHandler::showHelp},
    {"status", "显示系统状态", &CommandHandler::showStatus},
    {"data", "显示传感器数据", &CommandHandler::showSensorData},
    {"test", "测试网络连接", &CommandHandler::testNetwork},
    {"sync", "启停时间同步与拟合过程", &CommandHandler::toggleTimeSync},
    {"timesyncstatus", "显示时间同步状态", &CommandHandler::showTimeSyncStatus},
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
    {"realtime", "实时显示传感器数据", &CommandHandler::showRealtimeData},
    {"debug", "显示Debug信息", &CommandHandler::showDebugInfo},
    {"blue", "进入/退出蓝牙配置模式", &CommandHandler::toggleBluetoothConfig},
    {"testled", "测试LED (testled <1-3> <0-3>)", &CommandHandler::testLED},
    {"testbtn", "测试按钮 (testbtn <1-3>)", &CommandHandler::testButton}
};

CommandHandler::CommandHandler() {
    uartReceiver = nullptr;
    webSocketClient = nullptr;
    sensorData = nullptr;
    timeSync = nullptr;
    bluetoothConfig = nullptr;
    inputBuffer = "";
    
    Serial0.printf("[CommandHandler] Created\n");
}

CommandHandler::~CommandHandler() {
    Serial0.printf("[CommandHandler] Destroyed\n");
}

bool CommandHandler::initialize(UartReceiver* receiver, WebSocketClient* client, SensorData* data, TimeSync* timeSyncInstance) {
    uartReceiver = receiver;
    webSocketClient = client;
    sensorData = data;
    timeSync = timeSyncInstance;
    
    if (!uartReceiver || !webSocketClient || !sensorData || !timeSync) {
        Serial0.printf("[CommandHandler] ERROR: Invalid parameters\n");
        return false;
    }
    
    Serial0.printf("[CommandHandler] Initialized successfully\n");
    return true;
}

void CommandHandler::setBluetoothConfig(BluetoothConfig* btConfig) {
    bluetoothConfig = btConfig;
    if (bluetoothConfig) {
        Serial0.printf("[CommandHandler] BluetoothConfig module registered\n");
    }
}

void CommandHandler::processChar(char c) {
    // 如果在蓝牙配置模式，转发到蓝牙模块
    if (bluetoothConfig && bluetoothConfig->isConfigMode()) {
        // 收集完整的命令行
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                // 检查是否是退出命令
                if (inputBuffer.equalsIgnoreCase("BLUE")) {
                    // 退出配置模式
                    bluetoothConfig->setConfigMode(false);
                    inputBuffer = "";
                    return;
                }
                
                // 转发到蓝牙模块
                bluetoothConfig->forwardSerialData(inputBuffer);
                inputBuffer = "";
            }
        } else if (c >= 32 && c <= 126) {  // 可打印字符
            inputBuffer += c;
        }
    } else {
        // 正常CLI模式
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                processCommand(inputBuffer);
                inputBuffer = "";
            }
        } else if (c >= 32 && c <= 126) {  // 可打印字符
            inputBuffer += c;
        }
    }
}

void CommandHandler::processCommand(const String& command) {
    String args;
    String cmd = parseCommand(command, args);
    
    if (cmd.length() == 0) {
        return;
    }
    
    Serial0.printf("[CommandHandler] Processing command: %s\n", cmd.c_str());
    executeCommand(cmd, args);
}

void CommandHandler::showHelp(const String& args) {
    Serial0.printf("\n=== ESP32-S3 传感器网关 CLI 帮助 ===\n");
    Serial0.printf("可用命令:\n");
    
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        Serial0.printf("  %-10s - %s\n", commands[i].name.c_str(), commands[i].description.c_str());
    }
    
    Serial0.printf("\n示例:\n");
    Serial0.printf("  help                    - 显示此帮助信息\n");
    Serial0.printf("  status                  - 显示系统状态\n");
    Serial0.printf("  start                   - 开始数据采集\n");
    Serial0.printf("  device 2025001 1015     - 设置设备码和会话ID\n");
    Serial0.printf("  batch 50                - 设置批量大小为50\n");
    Serial0.printf("=====================================\n\n");
}

void CommandHandler::showStatus(const String& args) {
    Serial0.printf("\n=== 系统状态 ===\n");
    
    // 显示UART接收状态（减少栈使用）
    if (uartReceiver) {
        Serial0.printf("UART接收:\n");
        Serial0.printf("  总接收字节: %d\n", uartReceiver->getStats().totalBytesReceived);
        Serial0.printf("  解析帧数: %d\n", uartReceiver->getStats().totalFramesParsed);
        Serial0.printf("  解析错误: %d\n", uartReceiver->getStats().parseErrors);
        Serial0.printf("  传感器帧数统计:\n");
        for (int i = 0; i < 4; i++) {
            const char* sensorType = SensorData::getSensorType(i + 1);
            Serial0.printf("    %s (ID%d): %d frames\n", sensorType, i + 1, uartReceiver->getStats().sensorFrameCounts[i]);
        }
    }
    
    // 显示网络状态（减少栈使用）
    if (webSocketClient) {
        Serial0.printf("\n网络连接:\n");
        Serial0.printf("  服务器连接: %s\n", webSocketClient->getStats().serverConnected ? "已连接" : "未连接");
        Serial0.printf("  发送块数: %d\n", webSocketClient->getStats().totalBlocksSent);
        Serial0.printf("  发送字节: %d\n", webSocketClient->getStats().totalBytesSent);
        Serial0.printf("  发送速率: %.2f blocks/s\n", webSocketClient->getStats().avgSendRate);
        Serial0.printf("  连接尝试: %d\n", webSocketClient->getStats().connectionAttempts);
        Serial0.printf("  连接失败: %d\n", webSocketClient->getStats().connectionFailures);
    }
    
    // 显示传感器数据状态（减少栈使用）
    if (sensorData) {
        Serial0.printf("\n传感器数据:\n");
        Serial0.printf("  总帧数: %d\n", sensorData->getStats().totalFrames);
        Serial0.printf("  丢弃帧数: %d\n", sensorData->getStats().droppedFrames);
        Serial0.printf("  创建块数: %d\n", sensorData->getStats().blocksCreated);
        Serial0.printf("  发送块数: %d\n", sensorData->getStats().blocksSent);
        Serial0.printf("  平均帧率: %.2f fps\n", sensorData->getStats().avgFrameRate);
    }
    
    Serial0.printf("================\n\n");
}

void CommandHandler::showSensorData(const String& args) {
    Serial0.printf("\n=== 传感器数据 ===\n");
    
    if (sensorData) {
        SensorData::Stats dataStats = sensorData->getStats();
        Serial0.printf("数据统计:\n");
        Serial0.printf("  总帧数: %d\n", dataStats.totalFrames);
        Serial0.printf("  平均帧率: %.2f fps\n", dataStats.avgFrameRate);
        
        if (uartReceiver) {
            UartReceiver::Stats uartStats = uartReceiver->getStats();
            Serial0.printf("\n各传感器帧数:\n");
            for (int i = 0; i < 4; i++) {
                const char* sensorType = SensorData::getSensorType(i + 1);
                Serial0.printf("  %s (ID%d): %d frames\n", sensorType, i + 1, uartStats.sensorFrameCounts[i]);
            }
            
            if (uartStats.totalFramesParsed > 0) {
                Serial0.printf("\n最近接收状态:\n");
                Serial0.printf("  解析成功率: %.2f%%\n", 
                    (float)uartStats.totalFramesParsed / (uartStats.totalFramesParsed + uartStats.parseErrors) * 100.0f);
            }
        }
        
        if (dataStats.totalFrames == 0) {
            Serial0.printf("\n提示: 暂无传感器数据，请检查:\n");
            Serial0.printf("  1. 传感器是否正常工作\n");
            Serial0.printf("  2. UART连接是否正常\n");
            Serial0.printf("  3. 数据采集是否已启动\n");
        }
    } else {
        Serial0.printf("传感器数据管理器未初始化\n");
    }
    
    Serial0.printf("==================\n\n");
}

void CommandHandler::testNetwork(const String& args) {
    Serial0.printf("\n=== 网络测试 ===\n");
    
    if (webSocketClient) {
        if (webSocketClient->isConnected()) {
            Serial0.printf("网络连接正常\n");
            webSocketClient->sendHeartbeat();
        } else {
            Serial0.printf("网络连接异常，尝试重连...\n");
            webSocketClient->connect();
        }
    } else {
        Serial0.printf("WebSocket客户端未初始化\n");
    }
    
    Serial0.printf("===============\n\n");
}

void CommandHandler::syncTime(const String& args) {
    // 直接调用toggleTimeSync方法
    toggleTimeSync(args);
}

void CommandHandler::toggleTimeSync(const String& args) {
    if (!timeSync) {
        Serial0.printf("ERROR: 时间同步模块未初始化\n");
        return;
    }
    
    // 直接检查时间同步是否激活，而不是通过NTP偏移判断
    bool isActive = timeSync->isTimeSyncActive();
    
    if (isActive) {
        // 停止时间同步和拟合
        Serial0.printf("\n=== 停止时间同步与拟合 ===\n");
        timeSync->stopTimeSync();
        timeSync->stopBackgroundFitting();
        Serial0.printf("时间同步与拟合过程已停止\n");
        Serial0.printf("======================\n\n");
    } else {
        // 启动时间同步和拟合
        Serial0.printf("\n=== 开始时间同步与拟合 ===\n");
        if (timeSync->startTimeSync()) {
            // 重置计算状态，重新开始计算
            timeSync->resetCalculationState();
            timeSync->startBackgroundFitting();
            Serial0.printf("时间同步与拟合过程已开始\n");
            Serial0.printf("正在同步NTP时间...\n");
            Serial0.printf("拟合计算将在后台任务中每%d秒执行一次\n", Config::TIME_SYNC_CALC_INTERVAL_MS / 1000);
            Serial0.printf("每个传感器将进行%d次计算后取平均值\n", Config::TIME_SYNC_CALC_COUNT);
            Serial0.printf("请等待传感器数据收集以计算线性回归参数\n");
        } else {
            Serial0.printf("ERROR: 启动时间同步失败\n");
            Serial0.printf("请检查网络连接和WiFi状态\n");
        }
        Serial0.printf("======================\n\n");
    }
}

void CommandHandler::showTimeSyncStatus(const String& args) {
    Serial0.printf("\n=== 时间同步状态 ===\n");
    
    if (timeSync) {
        TimeSync::Stats stats = timeSync->getStats();
        
        Serial0.printf("NTP偏移: %lld ms\n", stats.ntpOffset);
        Serial0.printf("总数据对数量: %d/%d\n", stats.validPairs, stats.windowSize);
        Serial0.printf("最后更新: %d ms前\n", millis() - stats.lastUpdateTime);
        
        Serial0.printf("\n各传感器状态:\n");
        for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            Serial0.printf("  传感器%d: %s (a=%.6f, b=%.2f)\n", 
                         i + 1, 
                         stats.syncReady[i] ? "就绪" : "未就绪",
                         stats.linearParamA[i], 
                         stats.linearParamB[i]);
        }
        
        // 检查是否有任何传感器就绪
        bool anyReady = false;
        for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            if (stats.syncReady[i]) {
                anyReady = true;
                break;
            }
        }
        
        if (anyReady) {
            Serial0.printf("\n时间戳计算公式: T = a * S + b + N\n");
            Serial0.printf("其中: S = 传感器时间(ms), T = 全局时间戳(ms), N = NTP偏移(ms)\n");
            Serial0.printf("每个传感器有独立的参数 a 和 b\n");
        } else {
            Serial0.printf("\n时间同步未就绪，需要更多数据点进行计算\n");
            Serial0.printf("建议每个传感器至少收集10个有效数据对\n");
        }
        
        Serial0.printf("\n使用说明:\n");
        Serial0.printf("  1. 执行 'sync' 一键启停时间同步与拟合\n");
        Serial0.printf("  2. 等待收集足够数据后，时间同步将自动就绪\n");
        Serial0.printf("  3. 服务器端也可通过WebSocket发送sync命令控制\n");
    } else {
        Serial0.printf("ERROR: 时间同步模块未初始化\n");
    }
    
    Serial0.printf("==================\n\n");
}

void CommandHandler::setBatchSize(const String& args) {
    Serial0.printf("\n=== 设置批量大小 ===\n");
    if (args.length() > 0) {
        uint8_t size = args.toInt();
        if (size > 0 && size <= 100) {
            Serial0.printf("设置批量大小为: %d\n", size);
            Serial0.printf("注意: 此设置将在下次重启后生效\n");
            Serial0.printf("当前默认批量大小: 50\n");
        } else {
            Serial0.printf("错误: 批量大小必须在1-100之间\n");
        }
    } else {
        Serial0.printf("用法: batch <size>\n");
        Serial0.printf("示例: batch 50\n");
        Serial0.printf("当前默认批量大小: 50\n");
    }
    Serial0.printf("==================\n\n");
}

void CommandHandler::startCollection(const String& args) {
    Serial0.printf("\n=== 开始数据采集 ===\n");
    
    if (webSocketClient) {
        webSocketClient->startCollection();
        Serial0.printf("数据采集已开始\n");
    } else {
        Serial0.printf("WebSocket客户端未初始化\n");
    }
    
    Serial0.printf("==================\n\n");
}

void CommandHandler::stopCollection(const String& args) {
    Serial0.printf("\n=== 停止数据采集 ===\n");
    
    if (webSocketClient) {
        webSocketClient->stopCollection();
        Serial0.printf("数据采集已停止\n");
    } else {
        Serial0.printf("WebSocket客户端未初始化\n");
    }
    
    Serial0.printf("==================\n\n");
}

void CommandHandler::resetStats(const String& args) {
    Serial0.printf("\n=== 重置统计信息 ===\n");
    
    if (uartReceiver) {
        uartReceiver->resetStats();
    }
    if (webSocketClient) {
        webSocketClient->resetStats();
    }
    if (sensorData) {
        sensorData->resetStats();
    }
    
    Serial0.printf("所有统计信息已重置\n");
    Serial0.printf("==================\n\n");
}

void CommandHandler::showStats(const String& args) {
    showStatus(); // 复用状态显示功能
}

void CommandHandler::setDeviceInfo(const String& args) {
    Serial0.printf("\n=== 设置设备信息 ===\n");
    
    if (args.length() > 0) {
        int spaceIndex = args.indexOf(' ');
        if (spaceIndex > 0) {
            String deviceCode = args.substring(0, spaceIndex);
            String sessionId = args.substring(spaceIndex + 1);
            
            if (webSocketClient) {
                webSocketClient->setDeviceInfo(deviceCode, sessionId);
                Serial0.printf("设备码: %s\n", deviceCode.c_str());
                Serial0.printf("会话ID: %s\n", sessionId.c_str());
            } else {
                Serial0.printf("WebSocket客户端未初始化\n");
            }
        } else {
            Serial0.printf("用法: device <device_code> <session_id>\n");
        }
    } else {
        Serial0.printf("用法: device <device_code> <session_id>\n");
    }
    
    Serial0.printf("==================\n\n");
}

void CommandHandler::testUart(const String& args) {
    Serial0.printf("\n=== UART测试 ===\n");
    
    if (uartReceiver) {
        UartReceiver::Stats stats = uartReceiver->getStats();
        Serial0.printf("UART状态:\n");
        Serial0.printf("  总接收字节: %d\n", stats.totalBytesReceived);
        Serial0.printf("  解析帧数: %d\n", stats.totalFramesParsed);
        Serial0.printf("  解析错误: %d\n", stats.parseErrors);
        
        Serial0.printf("\n传感器帧数统计:\n");
        for (int i = 0; i < 4; i++) {
            const char* sensorType = SensorData::getSensorType(i + 1);
            Serial0.printf("  %s (ID%d): %d frames\n", sensorType, i + 1, stats.sensorFrameCounts[i]);
        }
        
        if (stats.totalFramesParsed == 0) {
            Serial0.printf("\n警告: 未接收到任何有效帧，请检查:\n");
            Serial0.printf("  1. 传感器是否正确连接\n");
            Serial0.printf("  2. 波特率是否匹配 (460800)\n");
            Serial0.printf("  3. 数据格式是否正确 (43字节帧)\n");
        }
    } else {
        Serial0.printf("UART接收器未初始化\n");
    }
    
    Serial0.printf("===============\n\n");
}

void CommandHandler::showBufferStatus(const String& args) {
    Serial0.printf("\n=== 缓冲区状态 ===\n");
    
    if (sensorData) {
        SensorData::Stats dataStats = sensorData->getStats();
        Serial0.printf("传感器数据缓冲区:\n");
        Serial0.printf("  总帧数: %d\n", dataStats.totalFrames);
        Serial0.printf("  丢弃帧数: %d\n", dataStats.droppedFrames);
        Serial0.printf("  创建块数: %d\n", dataStats.blocksCreated);
        Serial0.printf("  释放块数: %d\n", dataStats.blocksSent);
        Serial0.printf("  平均帧率: %.2f fps\n", dataStats.avgFrameRate);
        
        if (dataStats.droppedFrames > 0) {
            float dropRate = (float)dataStats.droppedFrames / (dataStats.totalFrames + dataStats.droppedFrames) * 100.0f;
            Serial0.printf("  丢帧率: %.2f%%\n", dropRate);
        }
    }
    
    if (uartReceiver) {
        UartReceiver::Stats uartStats = uartReceiver->getStats();
        Serial0.printf("\nUART接收缓冲区:\n");
        Serial0.printf("  总接收字节: %d\n", uartStats.totalBytesReceived);
        Serial0.printf("  解析帧数: %d\n", uartStats.totalFramesParsed);
        Serial0.printf("  解析错误: %d\n", uartStats.parseErrors);
        
        if (uartStats.parseErrors > 0) {
            float errorRate = (float)uartStats.parseErrors / (uartStats.totalFramesParsed + uartStats.parseErrors) * 100.0f;
            Serial0.printf("  解析错误率: %.2f%%\n", errorRate);
        }
    }
    
    Serial0.printf("==================\n\n");
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
    
    Serial0.printf("[CommandHandler] 未知命令: %s\n", command.c_str());
    Serial0.printf("输入 'help' 查看可用命令\n");
}

void CommandHandler::showSensorTypes(const String& args) {
    Serial0.printf("\n=== 传感器类型 ===\n");
    Serial0.printf("ID 1: waist (腰部)\n");
    Serial0.printf("ID 2: shoulder (肩部)\n");
    Serial0.printf("ID 3: wrist (手腕)\n");
    Serial0.printf("ID 4: racket (球拍)\n");
    Serial0.printf("==================\n\n");
}

void CommandHandler::showNetworkConfig(const String& args) {
    Serial0.printf("\n=== 网络配置 ===\n");
    Serial0.printf("WiFi SSID: %s\n", "xiaoming");
    Serial0.printf("服务器地址: %s:%d\n", "175.178.100.179", 8000);
    
    if (webSocketClient) {
        WebSocketClient::Stats netStats = webSocketClient->getStats();
        Serial0.printf("\n连接状态:\n");
        Serial0.printf("  服务器连接: %s\n", netStats.serverConnected ? "已连接" : "未连接");
        Serial0.printf("  连接尝试次数: %d\n", netStats.connectionAttempts);
        Serial0.printf("  连接失败次数: %d\n", netStats.connectionFailures);
        Serial0.printf("  发送块数: %d\n", netStats.totalBlocksSent);
        Serial0.printf("  发送字节数: %d\n", netStats.totalBytesSent);
        Serial0.printf("  发送速率: %.2f blocks/s\n", netStats.avgSendRate);
        Serial0.printf("  发送失败次数: %d\n", netStats.sendFailures);
        
        if (netStats.lastHeartbeat > 0) {
            uint32_t timeSinceHeartbeat = millis() - netStats.lastHeartbeat;
            Serial0.printf("  上次心跳: %d ms前\n", timeSinceHeartbeat);
        }
    }
    
    Serial0.printf("================\n\n");
}

void CommandHandler::showUartConfig(const String& args) {
    Serial0.printf("\n=== UART配置 ===\n");
    Serial0.printf("UART1: TX=17, RX=16, 460800波特率\n");
    Serial0.printf("数据格式: 43字节帧结构\n");
    Serial0.printf("帧头: 0xAA, 帧尾: 0x55\n");
    Serial0.printf("传感器ID: 1-4 (waist/shoulder/wrist/racket)\n");
    Serial0.printf("===============\n\n");
}

String CommandHandler::formatTimestamp(uint64_t timestamp) {
    uint64_t seconds = timestamp / 1000;
    uint32_t milliseconds = timestamp % 1000;
    
    return String(seconds) + "." + String(milliseconds);
}

String CommandHandler::formatFloat(float value, int precision) {
    return String(value, precision);
}

void CommandHandler::toggleDroppedPackets(const String& args) {
    Config::SHOW_DROPPED_PACKETS = !Config::SHOW_DROPPED_PACKETS;
    Serial0.printf("[CommandHandler] 显示丢弃数据包: %s\n", 
                  Config::SHOW_DROPPED_PACKETS ? "开启" : "关闭");
    
    if (Config::SHOW_DROPPED_PACKETS) {
        Serial0.printf("现在会显示丢弃数据包的详细信息\n");
    } else {
        Serial0.printf("现在不会显示丢弃数据包的详细信息\n");
    }
}

void CommandHandler::showDebugInfo(const String& args) {
    Config::DEBUG_PPRINT = !Config::DEBUG_PPRINT;
    Serial0.printf("[CommandHandler] 显示Debug信息: %s\n", 
                  Config::DEBUG_PPRINT ? "开启" : "关闭");
    
    if (Config::DEBUG_PPRINT) {
        Serial0.printf("现在会显示更多调试信息\n");
    } else {
        Serial0.printf("现在不会显示额外的调试信息\n");
    }
}

void CommandHandler::showRealtimeData(const String& args) {
    realtimeDataEnabled = !realtimeDataEnabled;
    Serial0.printf("[CommandHandler] 实时数据显示: %s\n", 
                  realtimeDataEnabled ? "开启" : "关闭");
    
    if (realtimeDataEnabled) {
        Serial0.printf("现在会实时显示解析出的传感器数据\n");
        Serial0.printf("按 Ctrl+C 或再次输入 'realtime' 停止显示\n");
        lastRealtimeDataTime = millis();
    } else {
        Serial0.printf("已停止实时数据显示\n");
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
    // if (now - lastRealtimeDataTime < 100) { // 100ms间隔
    //     return;
    // }
    lastRealtimeDataTime = now;
    
    // 显示传感器数据
    const char* sensorType = SensorData::getSensorType(frame.sensorId);
    Serial0.printf("[实时数据] %s(ID%d) - 时间戳:%u, 原始时间戳:%llu, 加速度:[%.2f,%.2f,%.2f], 角速度:[%.2f,%.2f,%.2f], 角度:[%.2f,%.2f,%.2f]\n",
                  sensorType, frame.sensorId, frame.timestamp, frame.rawTimestamp,
                  frame.acc[0], frame.acc[1], frame.acc[2],
                  frame.gyro[0], frame.gyro[1], frame.gyro[2],
                  frame.angle[0], frame.angle[1], frame.angle[2]);
}

void CommandHandler::toggleBluetoothConfig(const String& args) {
    if (!bluetoothConfig) {
        Serial0.printf("[CommandHandler] ERROR: BluetoothConfig module not initialized\n");
        return;
    }
    
    // 切换配置模式
    bool currentMode = bluetoothConfig->isConfigMode();
    bluetoothConfig->setConfigMode(!currentMode);
}

void CommandHandler::testLED(const String& args) {
    Serial0.printf("\n=== LED测试 ===\n");
    
    if (!bluetoothConfig) {
        Serial0.printf("ERROR: BluetoothConfig module not initialized\n");
        Serial0.printf("===============\n\n");
        return;
    }
    
    if (args.length() == 0) {
        Serial0.printf("用法: testled <LED编号1-3> <状态0-3>\n");
        Serial0.printf("状态: 0=关闭, 1=常亮, 2=慢闪, 3=快闪\n");
        Serial0.printf("示例: testled 1 1  (LED1常亮)\n");
        Serial0.printf("      testled 2 2  (LED2慢闪)\n");
        Serial0.printf("===============\n\n");
        return;
    }
    
    // 解析参数
    int spaceIndex = args.indexOf(' ');
    if (spaceIndex > 0) {
        uint8_t ledIndex = args.substring(0, spaceIndex).toInt();
        uint8_t stateValue = args.substring(spaceIndex + 1).toInt();
        
        if (ledIndex < 1 || ledIndex > 3) {
            Serial0.printf("ERROR: LED编号必须是1-3\n");
            Serial0.printf("===============\n\n");
            return;
        }
        
        if (stateValue > 3) {
            Serial0.printf("ERROR: 状态值必须是0-3\n");
            Serial0.printf("===============\n\n");
            return;
        }
        
        LEDState state = static_cast<LEDState>(stateValue);
        bluetoothConfig->testSetLED(ledIndex - 1, state);
    } else {
        Serial0.printf("ERROR: 缺少参数\n");
        Serial0.printf("用法: testled <LED编号1-3> <状态0-3>\n");
    }
    
    Serial0.printf("===============\n\n");
}

void CommandHandler::testButton(const String& args) {
    Serial0.printf("\n=== 按钮测试 ===\n");
    
    if (!bluetoothConfig) {
        Serial0.printf("ERROR: BluetoothConfig module not initialized\n");
        Serial0.printf("===============\n\n");
        return;
    }
    
    if (args.length() == 0) {
        Serial0.printf("读取所有按钮状态:\n");
        for (uint8_t i = 0; i < 3; i++) {
            bluetoothConfig->testReadButton(i);
        }
    } else {
        uint8_t btnIndex = args.toInt();
        if (btnIndex < 1 || btnIndex > 3) {
            Serial0.printf("ERROR: 按钮编号必须是1-3\n");
        } else {
            bluetoothConfig->testReadButton(btnIndex - 1);
        }
    }
    
    Serial0.printf("===============\n\n");
}

