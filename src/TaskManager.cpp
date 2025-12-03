#include "TaskManager.h"
#include "Config.h"

TaskManager::TaskManager() {
    uartReceiver = nullptr;
    webSocketClient = nullptr;
    commandHandler = nullptr;
    timeSync = nullptr;
    bufferPool = nullptr;
    sensorData = nullptr;
    bluetoothConfig = nullptr;
    
    uartTaskHandle = nullptr;
    networkTaskHandle = nullptr;
    cliTaskHandle = nullptr;
    monitorTaskHandle = nullptr;
    timeSyncTaskHandle = nullptr;
    bluetoothConfigTaskHandle = nullptr;
    
    tasksRunning = false;
    
    Serial0.printf("[TaskManager] Created\n");
}

TaskManager::~TaskManager() {
    stopTasks();
    
    if (uartReceiver) delete uartReceiver;
    if (webSocketClient) delete webSocketClient;
    if (commandHandler) delete commandHandler;
    if (timeSync) delete timeSync;
    if (bufferPool) delete bufferPool;
    if (sensorData) delete sensorData;
    if (bluetoothConfig) delete bluetoothConfig;
    
    Serial0.printf("[TaskManager] Destroyed\n");
}

bool TaskManager::initialize() {
    Serial0.printf("[TaskManager] Initializing...\n");
    
    // 创建模块实例
    timeSync = new TimeSync();
    if (!timeSync || !timeSync->initialize()) {
        Serial0.printf("[TaskManager] ERROR: Failed to initialize TimeSync\n");
        return false;
    }
    
    bufferPool = new BufferPool();
    if (!bufferPool || !bufferPool->initialize(20)) {
        Serial0.printf("[TaskManager] ERROR: Failed to initialize BufferPool\n");
        return false;
    }
    
    sensorData = new SensorData(bufferPool);
    if (!sensorData) {
        Serial0.printf("[TaskManager] ERROR: Failed to initialize SensorData\n");
        return false;
    }
    
    uartReceiver = new UartReceiver();
    if (!uartReceiver || !uartReceiver->initialize(sensorData, timeSync)) {
        Serial0.printf("[TaskManager] ERROR: Failed to initialize UartReceiver\n");
        return false;
    }
    
    webSocketClient = new WebSocketClient();
    if (!webSocketClient) {
        Serial0.printf("[TaskManager] ERROR: Failed to initialize WebSocketClient\n");
        return false;
    }
    
    commandHandler = new CommandHandler();
    if (!commandHandler || !commandHandler->initialize(uartReceiver, webSocketClient, sensorData, timeSync)) {
        Serial0.printf("[TaskManager] ERROR: Failed to initialize CommandHandler\n");
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
    
    // 初始化蓝牙配置模块
    bluetoothConfig = new BluetoothConfig();
    if (!bluetoothConfig || !bluetoothConfig->initialize()) {
        Serial0.printf("[TaskManager] ERROR: Failed to initialize BluetoothConfig\n");
        return false;
    }
    
    // 将BluetoothConfig实例传递给其他模块
    if (commandHandler && bluetoothConfig) {
        commandHandler->setBluetoothConfig(bluetoothConfig);
    }
    
    if (uartReceiver && bluetoothConfig) {
        uartReceiver->setBluetoothConfig(bluetoothConfig);
        bluetoothConfig->setUartReceiver(uartReceiver);  // 双向关联，用于帧数检测
    }
    
    Serial0.printf("[TaskManager] All modules initialized successfully\n");
    return true;
}

bool TaskManager::startTasks() {
    if (tasksRunning) {
        Serial0.printf("[TaskManager] Tasks already running\n");
        return true;
    }
    
    Serial0.printf("[TaskManager] Starting tasks...\n");
    
    // 创建任务
    if (!createUartTask()) {
        Serial0.printf("[TaskManager] ERROR: Failed to create UART task\n");
        return false;
    }
    
    if (!createNetworkTask()) {
        Serial0.printf("[TaskManager] ERROR: Failed to create network task\n");
        return false;
    }
    
    if (!createCliTask()) {
        Serial0.printf("[TaskManager] ERROR: Failed to create CLI task\n");
        return false;
    }
    
    if (!createMonitorTask()) {
        Serial0.printf("[TaskManager] ERROR: Failed to create monitor task\n");
        return false;
    }
    
    if (!createTimeSyncTask()) {
        Serial0.printf("[TaskManager] ERROR: Failed to create time sync task\n");
        return false;
    }
    
    if (!createBluetoothConfigTask()) {
        Serial0.printf("[TaskManager] ERROR: Failed to create bluetooth config task\n");
        return false;
    }
    
    tasksRunning = true;
    Serial0.printf("[TaskManager] All tasks started successfully\n");
    
    return true;
}

void TaskManager::stopTasks() {
    if (!tasksRunning) {
        return;
    }
    
    Serial0.printf("[TaskManager] Stopping tasks...\n");
    
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
    
    if (bluetoothConfigTaskHandle) {
        vTaskDelete(bluetoothConfigTaskHandle);
        bluetoothConfigTaskHandle = nullptr;
    }
    
    tasksRunning = false;
    Serial0.printf("[TaskManager] All tasks stopped\n");
}

void TaskManager::getSystemStatus() {
    Serial0.printf("\n=== 系统状态 ===\n");
    Serial0.printf("任务状态: %s\n", tasksRunning ? "运行中" : "已停止");
    Serial0.printf("UART任务: %s\n", uartTaskHandle ? "运行中" : "未运行");
    Serial0.printf("网络任务: %s\n", networkTaskHandle ? "运行中" : "未运行");
    Serial0.printf("CLI任务: %s\n", cliTaskHandle ? "运行中" : "未运行");
    Serial0.printf("监控任务: %s\n", monitorTaskHandle ? "运行中" : "未运行");
    Serial0.printf("时间同步任务: %s\n", timeSyncTaskHandle ? "运行中" : "未运行");
    Serial0.printf("蓝牙配置任务: %s\n", bluetoothConfigTaskHandle ? "运行中" : "未运行");
    if (bluetoothConfig) {
        Serial0.printf("蓝牙配置模式: %s\n", bluetoothConfig->isConfigMode() ? "已启用" : "未启用");
    }
    
    Serial0.printf("================\n\n");
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
        Serial0.printf("[TaskManager] ERROR: Failed to create UART task\n");
        return false;
    }
    
    Serial0.printf("[TaskManager] UART task created on Core 0\n");
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
        1  // Core 
    );
    
    if (result != pdPASS) {
        Serial0.printf("[TaskManager] ERROR: Failed to create network task\n");
        return false;
    }
    
    Serial0.printf("[TaskManager] Network task created on Core 1\n");
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
        Serial0.printf("[TaskManager] ERROR: Failed to create CLI task\n");
        return false;
    }
    
    Serial0.printf("[TaskManager] CLI task created on Core 1\n");
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
        1  // Core 1
    );
    
    if (result != pdPASS) {
        Serial0.printf("[TaskManager] ERROR: Failed to create monitor task\n");
        return false;
    }
    
    Serial0.printf("[TaskManager] Monitor task created on Core 1\n");
    return true;
}

void TaskManager::uartTaskLoop() {
    Serial0.printf("[UART_Task] Started on Core %d\n", xPortGetCoreID());
    
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
    Serial0.printf("[Network_Task] Started on Core %d\n", xPortGetCoreID());
    
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
            Serial0.printf("[Network_Task] WiFi connected, will start time sync in 5 seconds\n");
        }
        
        // 在WiFi连接后延迟5秒启动时间同步
        if (wifiConnected && !timeSyncStarted && (millis() - wifiConnectTime >= 5000)) {
            timeSyncStarted = true;
            Serial0.printf("[Network_Task] Starting time synchronization after WiFi delay...\n");
            if (timeSync && timeSync->startTimeSync()) {
                timeSync->startBackgroundFitting();
                Serial0.printf("[Network_Task] Time synchronization started successfully\n");
            } else {
                Serial0.printf("[Network_Task] WARNING: Failed to start time synchronization\n");
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
                        Serial0.printf("[TaskManager] WARNING: Failed to send data block, released to avoid memory leak\n");
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
    Serial0.printf("[CLI_Task] Started on Core %d\n", xPortGetCoreID());
    
    uint32_t lastPromptTime = 0;
    bool promptShown = false;
    
    while (true) {
        // 显示CLI提示符（每5秒显示一次，直到用户开始输入）
        // 但在蓝牙配置模式下不显示提示符
        if (!promptShown && millis() - lastPromptTime > 5000) {
            if (!bluetoothConfig || !bluetoothConfig->isConfigMode()) {
                Serial0.printf("\nESP32-S3> ");
                lastPromptTime = millis();
                promptShown = true;
            }
        }
        
        // CLI任务处理串口命令输入
        if (Serial0.available()) {
            promptShown = false; // 用户开始输入，重置提示符标志
            
            char c = Serial0.read();
            
            if(Config::DEBUG_PPRINT){
                Serial0.printf("[CLI_Task] Received char: 0x%02X ('%c')\n", c, (c >= 32 && c <= 126) ? c : '?');
            }
            
            // 使用CommandHandler的processChar方法处理字符
            // 它会自动处理正常命令和蓝牙配置模式
            if (commandHandler) {
                commandHandler->processChar(c);
                
                // 如果完成了一个命令（换行），显示提示符
                if ((c == '\n' || c == '\r') && (!bluetoothConfig || !bluetoothConfig->isConfigMode())) {
                    Serial0.printf("ESP32-S3> ");
                }
            } else {
                Serial0.printf("[CLI_Task] ERROR: commandHandler is null!\n");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms延迟
    }
}

void TaskManager::monitorTaskLoop() {
    Serial0.printf("[Monitor_Task] Started on Core %d\n", xPortGetCoreID());
    
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
        Serial0.printf("[TaskManager] ERROR: Failed to create time sync task\n");
        return false;
    }
    
    Serial0.printf("[TaskManager] Time sync task created successfully\n");
    return true;
}

void TaskManager::timeSyncTask(void* parameter) {
    TaskManager* manager = (TaskManager*)parameter;
    manager->timeSyncTaskLoop();
}

void TaskManager::timeSyncTaskLoop() {
    Serial0.printf("[TimeSync_Task] Started on Core %d\n", xPortGetCoreID());
    
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

bool TaskManager::createBluetoothConfigTask() {
    BaseType_t result = xTaskCreatePinnedToCore(
        bluetoothConfigTask,
        "BT_Config_Task",
        BLUETOOTH_CONFIG_TASK_STACK_SIZE,
        this,
        BLUETOOTH_CONFIG_TASK_PRIORITY,
        &bluetoothConfigTaskHandle,
        1  // 运行在Core 1上
    );
    
    if (result != pdPASS) {
        Serial0.printf("[TaskManager] ERROR: Failed to create bluetooth config task\n");
        return false;
    }
    
    Serial0.printf("[TaskManager] Bluetooth config task created on Core 1\n");
    return true;
}

void TaskManager::bluetoothConfigTask(void* parameter) {
    TaskManager* manager = (TaskManager*)parameter;
    manager->bluetoothConfigTaskLoop();
}

void TaskManager::bluetoothConfigTaskLoop() {
    Serial0.printf("[BluetoothConfig_Task] Started on Core %d\n", xPortGetCoreID());
    Serial0.printf("[BluetoothConfig_Task] 按钮: 1=%d, 2=%d, 3=%d\n", 3, 19, 16);
    Serial0.printf("[BluetoothConfig_Task] LED: 1=%d, 2=%d, 3=%d\n", 9, 20, 8);
    Serial0.printf("[BluetoothConfig_Task] 发送 'BLUE' 命令进入/退出配置模式\n");
    
    while (true) {
        // 调用BluetoothConfig的主循环
        if (bluetoothConfig) {
            bluetoothConfig->loop();
        }
        
        // 快速响应，1ms延迟（保证按钮响应及时，不阻塞UART接收）
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
