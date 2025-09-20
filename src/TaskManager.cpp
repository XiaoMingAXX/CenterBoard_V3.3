#include "TaskManager.h"
#include "Config.h"

TaskManager::TaskManager() {
    uartReceiver = nullptr;
    webSocketClient = nullptr;
    commandHandler = nullptr;
    timeSync = nullptr;
    bufferPool = nullptr;
    sensorData = nullptr;
    
    uartTaskHandle = nullptr;
    networkTaskHandle = nullptr;
    cliTaskHandle = nullptr;
    monitorTaskHandle = nullptr;
    timeSyncTaskHandle = nullptr;
    
    tasksRunning = false;
    
    Serial.printf("[TaskManager] Created\n");
}

TaskManager::~TaskManager() {
    stopTasks();
    
    if (uartReceiver) delete uartReceiver;
    if (webSocketClient) delete webSocketClient;
    if (commandHandler) delete commandHandler;
    if (timeSync) delete timeSync;
    if (bufferPool) delete bufferPool;
    if (sensorData) delete sensorData;
    
    Serial.printf("[TaskManager] Destroyed\n");
}

bool TaskManager::initialize() {
    Serial.printf("[TaskManager] Initializing...\n");
    
    // 创建模块实例
    timeSync = new TimeSync();
    if (!timeSync || !timeSync->initialize()) {
        Serial.printf("[TaskManager] ERROR: Failed to initialize TimeSync\n");
        return false;
    }
    
    bufferPool = new BufferPool();
    if (!bufferPool || !bufferPool->initialize(20)) {
        Serial.printf("[TaskManager] ERROR: Failed to initialize BufferPool\n");
        return false;
    }
    
    sensorData = new SensorData(bufferPool);
    if (!sensorData) {
        Serial.printf("[TaskManager] ERROR: Failed to initialize SensorData\n");
        return false;
    }
    
    uartReceiver = new UartReceiver();
    if (!uartReceiver || !uartReceiver->initialize(sensorData, timeSync)) {
        Serial.printf("[TaskManager] ERROR: Failed to initialize UartReceiver\n");
        return false;
    }
    
    webSocketClient = new WebSocketClient();
    if (!webSocketClient) {
        Serial.printf("[TaskManager] ERROR: Failed to initialize WebSocketClient\n");
        return false;
    }
    
    commandHandler = new CommandHandler();
    if (!commandHandler || !commandHandler->initialize(uartReceiver, webSocketClient, sensorData, timeSync)) {
        Serial.printf("[TaskManager] ERROR: Failed to initialize CommandHandler\n");
        return false;
    }
    
    // 设置WebSocketClient的BufferPool实例用于正确释放数据块
    if (webSocketClient && bufferPool) {
        webSocketClient->setBufferPool(bufferPool);
    }
    
    // 设置WebSocketClient的CommandHandler实例用于处理服务器命令
    if (webSocketClient && commandHandler) {
        webSocketClient->setCommandHandler(commandHandler);
    }
    
    Serial.printf("[TaskManager] All modules initialized successfully\n");
    return true;
}

bool TaskManager::startTasks() {
    if (tasksRunning) {
        Serial.printf("[TaskManager] Tasks already running\n");
        return true;
    }
    
    Serial.printf("[TaskManager] Starting tasks...\n");
    
    // 创建任务
    if (!createUartTask()) {
        Serial.printf("[TaskManager] ERROR: Failed to create UART task\n");
        return false;
    }
    
    if (!createNetworkTask()) {
        Serial.printf("[TaskManager] ERROR: Failed to create network task\n");
        return false;
    }
    
    if (!createCliTask()) {
        Serial.printf("[TaskManager] ERROR: Failed to create CLI task\n");
        return false;
    }
    
    if (!createMonitorTask()) {
        Serial.printf("[TaskManager] ERROR: Failed to create monitor task\n");
        return false;
    }
    
    if (!createTimeSyncTask()) {
        Serial.printf("[TaskManager] ERROR: Failed to create time sync task\n");
        return false;
    }
    
    tasksRunning = true;
    Serial.printf("[TaskManager] All tasks started successfully\n");
    
    return true;
}

void TaskManager::stopTasks() {
    if (!tasksRunning) {
        return;
    }
    
    Serial.printf("[TaskManager] Stopping tasks...\n");
    
    // 删除任务
    if (uartTaskHandle) {
        vTaskDelete(uartTaskHandle);
        uartTaskHandle = nullptr;
    }
    
    if (networkTaskHandle) {
        vTaskDelete(networkTaskHandle);
        networkTaskHandle = nullptr;
    }
    
    if (cliTaskHandle) {
        vTaskDelete(cliTaskHandle);
        cliTaskHandle = nullptr;
    }
    
    if (monitorTaskHandle) {
        vTaskDelete(monitorTaskHandle);
        monitorTaskHandle = nullptr;
    }
    
    if (timeSyncTaskHandle) {
        vTaskDelete(timeSyncTaskHandle);
        timeSyncTaskHandle = nullptr;
    }
    
    tasksRunning = false;
    Serial.printf("[TaskManager] All tasks stopped\n");
}

void TaskManager::getSystemStatus() {
    Serial.printf("\n=== 系统状态 ===\n");
    Serial.printf("任务状态: %s\n", tasksRunning ? "运行中" : "已停止");
    Serial.printf("UART任务: %s\n", uartTaskHandle ? "运行中" : "未运行");
    Serial.printf("网络任务: %s\n", networkTaskHandle ? "运行中" : "未运行");
    Serial.printf("CLI任务: %s\n", cliTaskHandle ? "运行中" : "未运行");
    Serial.printf("监控任务: %s\n", monitorTaskHandle ? "运行中" : "未运行");
    Serial.printf("时间同步任务: %s\n", timeSyncTaskHandle ? "运行中" : "未运行");
    Serial.printf("================\n\n");
}

void TaskManager::uartTask(void* parameter) {
    TaskManager* manager = (TaskManager*)parameter;
    manager->uartTaskLoop();
}

void TaskManager::networkTask(void* parameter) {
    TaskManager* manager = (TaskManager*)parameter;
    manager->networkTaskLoop();
}

void TaskManager::cliTask(void* parameter) {
    TaskManager* manager = (TaskManager*)parameter;
    manager->cliTaskLoop();
}

void TaskManager::monitorTask(void* parameter) {
    TaskManager* manager = (TaskManager*)parameter;
    manager->monitorTaskLoop();
}

bool TaskManager::createUartTask() {
    BaseType_t result = xTaskCreatePinnedToCore(
        uartTask,
        "UART_Task",
        UART_TASK_STACK_SIZE,
        this,
        UART_TASK_PRIORITY,
        &uartTaskHandle,
        0  // Core 0
    );
    
    if (result != pdPASS) {
        Serial.printf("[TaskManager] ERROR: Failed to create UART task\n");
        return false;
    }
    
    Serial.printf("[TaskManager] UART task created on Core 0\n");
    return true;
}

bool TaskManager::createNetworkTask() {
    BaseType_t result = xTaskCreatePinnedToCore(
        networkTask,
        "Network_Task",
        NETWORK_TASK_STACK_SIZE,
        this,
        NETWORK_TASK_PRIORITY,
        &networkTaskHandle,
        0  // Core 1
    );
    
    if (result != pdPASS) {
        Serial.printf("[TaskManager] ERROR: Failed to create network task\n");
        return false;
    }
    
    Serial.printf("[TaskManager] Network task created on Core 1\n");
    return true;
}

bool TaskManager::createCliTask() {
    BaseType_t result = xTaskCreatePinnedToCore(
        cliTask,
        "CLI_Task",
        CLI_TASK_STACK_SIZE,
        this,
        CLI_TASK_PRIORITY,
        &cliTaskHandle,
        1  // Core 1
    );
    
    if (result != pdPASS) {
        Serial.printf("[TaskManager] ERROR: Failed to create CLI task\n");
        return false;
    }
    
    Serial.printf("[TaskManager] CLI task created on Core 1\n");
    return true;
}

bool TaskManager::createMonitorTask() {
    BaseType_t result = xTaskCreatePinnedToCore(
        monitorTask,
        "Monitor_Task",
        MONITOR_TASK_STACK_SIZE,
        this,
        MONITOR_TASK_PRIORITY,
        &monitorTaskHandle,
        0  // Core 1
    );
    
    if (result != pdPASS) {
        Serial.printf("[TaskManager] ERROR: Failed to create monitor task\n");
        return false;
    }
    
    Serial.printf("[TaskManager] Monitor task created on Core 1\n");
    return true;
}

void TaskManager::uartTaskLoop() {
    Serial.printf("[UART_Task] Started on Core %d\n", xPortGetCoreID());
    
    // 启动UART接收（现在使用中断方式）
    if (uartReceiver) {
        uartReceiver->start();
    }
    
    while (true) {
        // 处理DMA中断标记的数据
        if (uartReceiver) {
            uartReceiver->processDmaData();
        }
        
        // 可以在这里添加数据块处理逻辑
        // 例如：检查数据块是否已满，是否需要发送等
        
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms延迟，快速响应DMA数据
    }
}

void TaskManager::networkTaskLoop() {
    Serial.printf("[Network_Task] Started on Core %d\n", xPortGetCoreID());
    
    // 初始化网络连接
    if (webSocketClient) {
        // 从配置中读取网络参数
        webSocketClient->initialize(Config::WIFI_SSID, Config::WIFI_PASSWORD, 
                                   Config::SERVER_URL, Config::SERVER_PORT, 
                                   Config::DEVICE_CODE);
        webSocketClient->connect();
    }
    
    // 用于跟踪WiFi连接状态和延迟启动时间同步
    bool wifiConnected = false;
    bool timeSyncStarted = false;
    uint32_t wifiConnectTime = 0;
    
    while (true) {
        // 检查WiFi连接状态
        if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
            wifiConnected = true;
            wifiConnectTime = millis();
            Serial.printf("[Network_Task] WiFi connected, will start time sync in 5 seconds\n");
        }
        
        // 在WiFi连接后延迟5秒启动时间同步
        if (wifiConnected && !timeSyncStarted && (millis() - wifiConnectTime >= 5000)) {
            timeSyncStarted = true;
            Serial.printf("[Network_Task] Starting time synchronization after WiFi delay...\n");
            if (timeSync && timeSync->startTimeSync()) {
                timeSync->startBackgroundFitting();
                Serial.printf("[Network_Task] Time synchronization started successfully\n");
            } else {
                Serial.printf("[Network_Task] WARNING: Failed to start time synchronization\n");
            }
        }
        
        // 网络任务处理数据发送和服务器通信
        if (webSocketClient) {
            // 处理WebSocket事件
            webSocketClient->loop();
            
            // 处理发送队列
            webSocketClient->processSendQueue();
            
            // 处理连接重试
            webSocketClient->handleConnectionRetry();
            
            // 获取数据块并发送
            if (sensorData) {
                DataBlock* block = sensorData->getNextBlock();
                if (block) {
                    bool sendResult = webSocketClient->sendDataBlock(block);
                    if (!sendResult) {
                        // 如果发送失败（队列满或连接问题），需要立即释放数据块避免内存泄漏
                        sensorData->releaseBlock(block);
                        if(Config::SHOW_DROPPED_PACKETS){
                        Serial.printf("[TaskManager] WARNING: Failed to send data block, released to avoid memory leak\n");
                        }
                    }
                    // 注意：成功发送的数据块将在WebSocketClient::processSendQueue()中发送完成后释放
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms延迟
    }
}

void TaskManager::cliTaskLoop() {
    Serial.printf("[CLI_Task] Started on Core %d\n", xPortGetCoreID());
    
    String inputBuffer = "";
    uint32_t lastPromptTime = 0;
    bool promptShown = false;
    
    while (true) {
        // 显示CLI提示符（每5秒显示一次，直到用户开始输入）
        if (!promptShown && millis() - lastPromptTime > 5000) {
            Serial.printf("\nESP32-S3> ");
            lastPromptTime = millis();
            promptShown = true;
        }
        
        // CLI任务处理串口命令输入
        if (Serial.available()) {
            promptShown = false; // 用户开始输入，重置提示符标志
            
            char c = Serial.read();
            
            if(Config::DEBUG_PPRINT){
                Serial.printf("[CLI_Task] Received char: 0x%02X ('%c')\n", c, (c >= 32 && c <= 126) ? c : '?');
            }
            
            if (c == '\n' || c == '\r') {
                if (inputBuffer.length() > 0) {
                    Serial.printf("[CLI_Task] Processing command: '%s'\n", inputBuffer.c_str());
                    if (commandHandler) {
                        commandHandler->processCommand(inputBuffer);
                    } else {
                        Serial.printf("[CLI_Task] ERROR: commandHandler is null!\n");
                    }
                    inputBuffer = "";
                    Serial.printf("ESP32-S3> "); // 显示新的提示符
                }
            } else if (c >= 32 && c <= 126) { // 可打印字符
                inputBuffer += c;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms延迟
    }
}

void TaskManager::monitorTaskLoop() {
    Serial.printf("[Monitor_Task] Started on Core %d\n", xPortGetCoreID());
    
    uint32_t lastStatusTime = 0;
    const uint32_t STATUS_INTERVAL = 30000; // 30秒
    
    while (true) {
        uint32_t now = millis();
        
        // 定期显示系统状态
        if (now - lastStatusTime >= STATUS_INTERVAL) {
            getSystemStatus();
            lastStatusTime = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒延迟
    }
}

bool TaskManager::createTimeSyncTask() {
    BaseType_t result = xTaskCreatePinnedToCore(
        timeSyncTask,
        "TimeSyncTask",
        TIME_SYNC_TASK_STACK_SIZE,
        this,
        TIME_SYNC_TASK_PRIORITY,
        &timeSyncTaskHandle,
        0  // 运行在Core1上
    );
    
    if (result != pdPASS) {
        Serial.printf("[TaskManager] ERROR: Failed to create time sync task\n");
        return false;
    }
    
    Serial.printf("[TaskManager] Time sync task created successfully\n");
    return true;
}

void TaskManager::timeSyncTask(void* parameter) {
    TaskManager* manager = (TaskManager*)parameter;
    manager->timeSyncTaskLoop();
}

void TaskManager::timeSyncTaskLoop() {
    Serial.printf("[TimeSync_Task] Started on Core %d\n", xPortGetCoreID());
    
    uint32_t lastFittingTime = 0;
    const uint32_t FITTING_INTERVAL = Config::TIME_SYNC_CALC_INTERVAL_MS; // 使用配置的计算间隔
    
    while (true) {
        uint32_t now = millis();
        
        // 定期进行后台拟合计算
        if (now - lastFittingTime >= FITTING_INTERVAL) {
            if (timeSync) {
                timeSync->performBackgroundFitting();
            }
            lastFittingTime = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒延迟
    }
}
