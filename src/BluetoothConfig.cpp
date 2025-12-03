#include "BluetoothConfig.h"
#include "UartReceiver.h"
#include "TimeSync.h"

// 静态常量定义
const uint8_t BluetoothConfig::BUTTON_PINS[BUTTON_COUNT] = {3, 19, 16};
const uint8_t BluetoothConfig::LED_PINS[LED_COUNT] = {9, 20, 8};

// BLE DATA\r\n
const uint8_t BluetoothConfig::BLE_DATA_HEADER[10] = {
    0x42, 0x4C, 0x45, 0x20, 0x44, 0x41, 0x54, 0x41, 0x0D, 0x0A
};

// +RECEIVED:1,43\r\n
const uint8_t BluetoothConfig::BLE_DATA_FOOTER[16] = {
    0x2B, 0x52, 0x45, 0x43, 0x45, 0x49, 0x56, 0x45, 
    0x44, 0x3A, 0x31, 0x2C, 0x34, 0x33, 0x0D, 0x0A
};

BluetoothConfig::BluetoothConfig() {
    configMode = false;
    uartReceiver = nullptr;
    timeSync = nullptr;
    
    // 初始化环形缓冲区
    writePos = 0;
    readPos = 0;
    memset(uartRxBuffer, 0, sizeof(uartRxBuffer));
    bufferMutex = xSemaphoreCreateMutex();
    
    // 初始化配置行缓冲区
    configLineBufferPos = 0;
    memset(configLineBuffer, 0, sizeof(configLineBuffer));
    
    // 初始化按钮状态
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        buttons[i].pin = BUTTON_PINS[i];
        buttons[i].lastState = HIGH;  // 未按下时为高电平
        buttons[i].lastDebounceTime = 0;
        buttons[i].pressed = false;
    }
    
    // 初始化LED状态
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        leds[i].pin = LED_PINS[i];
        leds[i].state = LEDState::OFF;
        leds[i].lastToggleTime = 0;
        leds[i].currentLevel = false;
    }
    
    // 初始化蓝牙设备信息
    const char* macAddresses[DEVICE_COUNT] = {
        "BB:DD:E9:09:67:00",  // 设备1
        "EA:AA:DF:A8:54:00",  // 设备2
        "AC:A2:91:23:E5:00"   // 设备3
    };
    
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        devices[i].macAddress = macAddresses[i];
        devices[i].state = DeviceConnectionState::DISCONNECTED;
        devices[i].scanIndex = -1;
        devices[i].lastUpdateTime = 0;
    }
    
    // 初始化扫描状态
    isScanning = false;
    scanStartTime = 0;
    lastScanDataTime = 0;
    scanResultBuffer = "";
    
    // 初始化自动连接流程
    autoConnectState = AutoConnectState::IDLE;
    systemStartTime = millis();
    lastAutoScanTime = 0;
    autoScanCount = 0;
    
    // 初始化连接流程
    currentConnectingDevice = -1;
    connectRetryCount = 0;
    lastConnectAttemptTime = 0;
    connectStartTime = 0;
    
    // 初始化待连接设备列表
    pendingConnectCount = 0;
    memset(pendingConnectDevices, -1, sizeof(pendingConnectDevices));
    
    // 初始化帧数检测
    memset(lastFrameCounts, 0, sizeof(lastFrameCounts));
    lastConnectionCheckTime = 0;
}

BluetoothConfig::~BluetoothConfig() {
    // 清理资源
    if (bufferMutex) {
        vSemaphoreDelete(bufferMutex);
    }
}

bool BluetoothConfig::initialize() {
    Serial0.printf("[BluetoothConfig] Initializing...\n");
    
    // 初始化GPIO
    initGPIO();
    
    Serial0.printf("[BluetoothConfig] Initialized successfully\n");
    Serial0.printf("[BluetoothConfig] ========== Configuration Parameters ==========\n");
    Serial0.printf("[BluetoothConfig] Scan duration: %u sec\n", BT_SCAN_DURATION_SEC);
    Serial0.printf("[BluetoothConfig] Auto-connect start delay: %u ms\n", AUTO_CONNECT_START_DELAY_MS);
    Serial0.printf("[BluetoothConfig] Auto-scan interval: %u ms\n", AUTO_SCAN_INTERVAL_MS);
    Serial0.printf("[BluetoothConfig] Max auto-scan count: %u\n", MAX_AUTO_SCAN_COUNT);
    Serial0.printf("[BluetoothConfig] Connect retry interval: %u ms\n", CONNECT_RETRY_INTERVAL_MS);
    Serial0.printf("[BluetoothConfig] Max connect retry: %u\n", MAX_CONNECT_RETRY_COUNT);
    Serial0.printf("[BluetoothConfig] ============================================\n");
    Serial0.printf("[BluetoothConfig] Send 'BLUE' command to enter/exit config mode\n");
    Serial0.printf("[BluetoothConfig] Buttons: 1=%d, 2=%d, 3=%d\n", BUTTON_PINS[0], BUTTON_PINS[1], BUTTON_PINS[2]);
    Serial0.printf("[BluetoothConfig] LEDs: 1=%d, 2=%d, 3=%d\n", LED_PINS[0], LED_PINS[1], LED_PINS[2]);
    
    return true;
}

void BluetoothConfig::initGPIO() {
    // 初始化按钮引脚（输入，上拉）
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        pinMode(buttons[i].pin, INPUT_PULLUP);
        Serial0.printf("[BluetoothConfig] Button %d initialized on pin %d\n", i + 1, buttons[i].pin);
    }
    
    // 初始化LED引脚（输出）
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        pinMode(leds[i].pin, OUTPUT);
        digitalWrite(leds[i].pin, LOW);  // 初始关闭
        Serial0.printf("[BluetoothConfig] LED %d initialized on pin %d\n", i + 1, leds[i].pin);
    }
}

void BluetoothConfig::loop() {
    // 从环形缓冲区读取并解析配置数据（异步处理）
    readAndParseConfigData();
    
    // 处理按钮和LED
    handleButtonsAndLEDs();
    
    // 基于帧数检测连接状态（每2秒检查一次）
    checkConnectionByFrameCount();
    
    // 处理蓝牙业务逻辑（仅在非配置模式下）
    if (!configMode) {
        handleBluetoothBusiness();
    }
}

void BluetoothConfig::setConfigMode(bool enabled) {
    if (configMode == enabled) {
        return; // 状态未改变
    }
    
    configMode = enabled;
    
    if (configMode) {
        Serial0.printf("\n[BluetoothConfig] ===== 进入配置模式 =====\n");
        Serial0.printf("[BluetoothConfig] 现在可以发送AT指令到蓝牙模块\n");
        Serial0.printf("[BluetoothConfig] 发送'BLUE'退出配置模式\n");
        Serial0.printf("[BluetoothConfig] ========================\n\n");
    } else {
        Serial0.printf("\n[BluetoothConfig] ===== 退出配置模式 =====\n");
        Serial0.printf("[BluetoothConfig] 恢复正常工作模式\n");
        Serial0.printf("[BluetoothConfig] ========================\n\n");
    }
    
    // 清空行缓冲区
    configLineBufferPos = 0;
}

void BluetoothConfig::forwardSerialData(const uint8_t* data, size_t length) {
    if (!configMode || !data || length == 0) {
        return;
    }
    
    // 转发到UART1（蓝牙模块）
    uart_write_bytes(UART_NUM_1, (const char*)data, length);
}

void BluetoothConfig::forwardSerialData(const String& data) {
    if (!configMode || data.length() == 0) {
        return;
    }
    
    // 转发到UART1（蓝牙模块）
    uart_write_bytes(UART_NUM_1, data.c_str(), data.length());
    uart_write_bytes(UART_NUM_1, "\r\n", 2);  // 添加换行符
}

void BluetoothConfig::setUartReceiver(UartReceiver* receiver) {
    uartReceiver = receiver;
    if (uartReceiver) {
        Serial0.printf("[BluetoothConfig] UartReceiver instance registered for frame count detection\n");
    }
}

void BluetoothConfig::setTimeSync(TimeSync* ts) {
    timeSync = ts;
    if (timeSync) {
        Serial0.printf("[BluetoothConfig] TimeSync instance registered for calibration control\n");
    }
}
        
// 基于帧数检测连接状态
void BluetoothConfig::checkConnectionByFrameCount() {
    if (!uartReceiver) {
        return; // 没有UartReceiver实例，无法检测
    }
    
    uint32_t now = millis();
    
    // 每500ms检查一次
    if (now - lastConnectionCheckTime < CONNECTION_CHECK_INTERVAL_MS) {
        return;
        }
    
    lastConnectionCheckTime = now;
    
    // 获取当前帧数统计
    UartReceiver::Stats stats = uartReceiver->getStats();
    
    // 检查每个设备对应传感器的帧数变化
    // 设备1 -> 传感器1 (sensorFrameCounts[0])
    // 设备2 -> 传感器2 (sensorFrameCounts[1])
    // 设备3 -> 传感器3 (sensorFrameCounts[2])
    
    const uint8_t sensorMapping[DEVICE_COUNT] = {0, 1, 2}; // 传感器索引（0-based）
    
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        uint8_t sensorIndex = sensorMapping[i];
        uint32_t currentFrameCount = stats.sensorFrameCounts[sensorIndex];
        uint32_t lastFrameCount = lastFrameCounts[i];
        
        // 计算帧数增量
        uint32_t frameIncrease = currentFrameCount - lastFrameCount;
        
        // 更新记录的帧数
        lastFrameCounts[i] = currentFrameCount;
        
        // 根据帧数增量判断连接状态
        if (frameIncrease >= FRAME_INCREASE_THRESHOLD) {
            // 帧数在增加，说明设备已连接
            if (devices[i].state != DeviceConnectionState::CONNECTED) {
                Serial0.printf("[BluetoothConfig] Device %d detected CONNECTED by frame count (sensor %d: +%u frames in 500ms)\n", 
                              i + 1, sensorIndex + 1, frameIncrease);
                updateDeviceState(i, DeviceConnectionState::CONNECTED);
}
        } else {
            // 帧数没有增加，说明设备断开或未连接
            if (devices[i].state == DeviceConnectionState::CONNECTED) {
                Serial0.printf("[BluetoothConfig] Device %d detected DISCONNECTED by frame count (sensor %d: +%u frames in 500ms)\n", 
                              i + 1, sensorIndex + 1, frameIncrease);
                updateDeviceState(i, DeviceConnectionState::DISCONNECTED);
                devices[i].scanIndex = -1;  // 清除扫描索引
            }
        }
    }
}

void BluetoothConfig::handleButtonsAndLEDs() {
    // 更新所有按钮状态
    for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
        updateButton(i);
    }
    // setLEDState(1, LEDState::SLOW_BLINK);
    // setLEDState(2, LEDState::FAST_BLINK);
    // setLEDState(3, LEDState::ON);
    // 更新所有LED状态
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        updateLED(i);
    }
}

void BluetoothConfig::updateButton(uint8_t index) {
    if (index >= BUTTON_COUNT) return;
    
    ButtonState& btn = buttons[index];
    bool currentState = digitalRead(btn.pin);
    uint32_t now = millis();
    
    // 检测状态变化（低电平 = 按下）
    if (currentState != btn.lastState) {
        btn.lastDebounceTime = now;  // 状态改变，重置去抖计时器
        btn.lastState = currentState; // 立即更新lastState
    }
    
    // 去抖处理：状态稳定超过去抖时间后才处理
    if ((now - btn.lastDebounceTime) >= DEBOUNCE_TIME_MS) {
        // 检测按钮按下（低电平且之前未标记为按下）
        if (currentState == LOW && !btn.pressed) {
            btn.pressed = true;
            handleButtonPress(index);
        }
        // 检测按钮释放（高电平且之前标记为按下）
        else if (currentState == HIGH && btn.pressed) {
            btn.pressed = false;
        }
    }
}

void BluetoothConfig::handleButtonPress(uint8_t buttonIndex) {
    if (buttonIndex >= BUTTON_COUNT) return;
    
    Serial0.printf("[BluetoothConfig] Button %d pressed\n", buttonIndex + 1);
    
    // 在非配置模式下，处理蓝牙设备连接逻辑
    if (!configMode) {
        handleButtonPressForDevice(buttonIndex);
    } else {
        // 配置模式下，切换LED状态（用于测试）
    cycleLEDState(buttonIndex);
    }
}

void BluetoothConfig::updateLED(uint8_t index) {
    if (index >= LED_COUNT) return;
    
    LEDControl& led = leds[index];
    uint32_t now = millis();
    
    switch (led.state) {
        case LEDState::OFF:
            digitalWrite(led.pin, LOW);
            led.currentLevel = false;
            break;
            
        case LEDState::ON:
            digitalWrite(led.pin, HIGH);
            led.currentLevel = true;
            break;
            
        case LEDState::SLOW_BLINK:
            if (now - led.lastToggleTime >= SLOW_BLINK_INTERVAL_MS) {
                led.currentLevel = !led.currentLevel;
                digitalWrite(led.pin, led.currentLevel ? HIGH : LOW);
                led.lastToggleTime = now;
            }
            break;
            
        case LEDState::FAST_BLINK:
            if (now - led.lastToggleTime >= FAST_BLINK_INTERVAL_MS) {
                led.currentLevel = !led.currentLevel;
                digitalWrite(led.pin, led.currentLevel ? HIGH : LOW);
                led.lastToggleTime = now;
            }
            break;
    }
}

void BluetoothConfig::setLEDState(uint8_t index, LEDState newState) {
    if (index >= LED_COUNT) return;
    
    leds[index].state = newState;
    leds[index].lastToggleTime = millis();
    
    const char* stateNames[] = {"OFF", "ON", "SLOW_BLINK", "FAST_BLINK"};
    Serial0.printf("[BluetoothConfig] LED %d set to %s\n", index + 1, stateNames[(int)newState]);
}

void BluetoothConfig::cycleLEDState(uint8_t index) {
    if (index >= LED_COUNT) return;
    
    LEDControl& led = leds[index];
    
    // 状态循环：关闭 -> 慢闪 -> 快闪 -> 常亮 -> 关闭
    switch (led.state) {
        case LEDState::OFF:
            setLEDState(index, LEDState::SLOW_BLINK);
            break;
        case LEDState::SLOW_BLINK:
            setLEDState(index, LEDState::FAST_BLINK);
            break;
        case LEDState::FAST_BLINK:
            setLEDState(index, LEDState::ON);
            break;
        case LEDState::ON:
            setLEDState(index, LEDState::OFF);
            break;
    }
}

// 测试接口实现
void BluetoothConfig::testSetLED(uint8_t index, LEDState state) {
    if (index < LED_COUNT) {
        setLEDState(index, state);
        Serial0.printf("[BluetoothConfig] Test: LED %d set to state %d\n", index + 1, (int)state);
    } else {
        Serial0.printf("[BluetoothConfig] Test: Invalid LED index %d\n", index);
    }
}

bool BluetoothConfig::testReadButton(uint8_t index) {
    if (index >= BUTTON_COUNT) {
        Serial0.printf("[BluetoothConfig] Test: Invalid button index %d\n", index);
        return false;
    }
    
    bool state = digitalRead(buttons[index].pin);
    Serial0.printf("[BluetoothConfig] Test: Button %d (GPIO %d) = %s (raw: %d)\n", 
                  index + 1, buttons[index].pin, 
                  state == LOW ? "PRESSED" : "RELEASED", state);
    return state == LOW;
}

// =============== 蓝牙业务逻辑实现 ===============

void BluetoothConfig::handleBluetoothBusiness() {
    uint32_t now = millis();
    
    // 1. 检查扫描是否完成
    if (isScanning) {
        bool scanEnded = false;
        
        // 方式1：如果距离最后接收扫描数据超过1秒，认为扫描完成
        if (lastScanDataTime > 0 && (now - lastScanDataTime >= SCAN_DATA_TIMEOUT_MS)) {
            scanEnded = true;
            Serial0.printf("[BluetoothConfig] Scan completed (no more data for %u ms)\n", SCAN_DATA_TIMEOUT_MS);
        }
        // 方式2：绝对超时保护（7秒）
        else if (now - scanStartTime >= SCAN_TIMEOUT_MS) {
            scanEnded = true;
            Serial0.printf("[BluetoothConfig] Scan timeout (%u ms)\n", SCAN_TIMEOUT_MS);
        }
        
        if (scanEnded) {
            isScanning = false;
            
            // 处理累积的扫描结果
            if (scanResultBuffer.length() > 0) {
                processScanResult(scanResultBuffer);
            } else {
                Serial0.printf("[BluetoothConfig] No scan result received\n");
            }
            scanResultBuffer = "";
            
            // 重置所有仍处于SCANNING状态的设备为DISCONNECTED
            for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
                if (devices[i].state == DeviceConnectionState::SCANNING) {
                    updateDeviceState(i, DeviceConnectionState::DISCONNECTED);
                }
            }
        }
    }
    
    // 2. 自动连接流程状态机
    autoConnectProcess();
    
    // 3. 更新所有设备的LED状态
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        updateLEDByDeviceState(i);
    }
}

// ====== 环形缓冲区操作函数 ======

// 由UartReceiver快速调用，将数据写入环形缓冲区
void BluetoothConfig::writeUartDataToBuffer(const uint8_t* data, size_t length) {
    if (!data || length == 0 || !bufferMutex) {
        return;
    }
    
    // 获取互斥锁（超时10ms，防止阻塞UartReceiver太久）
    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // 无法获取锁，丢弃数据（防止阻塞UartReceiver）
    }
    
    // 写入数据到环形缓冲区
    for (size_t i = 0; i < length; i++) {
        uartRxBuffer[writePos] = data[i];
        writePos = (writePos + 1) % UART_RX_BUFFER_SIZE;
        
        // 检测缓冲区溢出
        if (writePos == readPos) {
            // 缓冲区满，移动读指针（丢弃最旧的数据）
            readPos = (readPos + 1) % UART_RX_BUFFER_SIZE;
        }
    }
    
    xSemaphoreGive(bufferMutex);
}

// 获取环形缓冲区中可读数据量
size_t BluetoothConfig::getAvailableDataCount() const {
    if (writePos >= readPos) {
        return writePos - readPos;
    } else {
        return UART_RX_BUFFER_SIZE - readPos + writePos;
    }
}

// 从环形缓冲区读取并解析配置数据（在loop中调用，异步处理）
void BluetoothConfig::readAndParseConfigData() {
    if (!bufferMutex) {
        return;
    }
    
    // 获取互斥锁
    if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(1)) != pdTRUE) {
        return; // 无法获取锁，下次再读
    }
    
    // 从环形缓冲区读取数据
    while (readPos != writePos) {
        uint8_t byte = uartRxBuffer[readPos];
        readPos = (readPos + 1) % UART_RX_BUFFER_SIZE;
        
        // 累积到行缓冲区
        if (configLineBufferPos < CONFIG_LINE_BUFFER_SIZE - 1) {
            configLineBuffer[configLineBufferPos++] = byte;
        }
        
        // 遇到换行符，处理一行
        if (byte == '\n') {
            // 释放锁（处理期间不锁定缓冲区）
            xSemaphoreGive(bufferMutex);
            
            // 终止字符串
            configLineBuffer[configLineBufferPos] = 0;
            String line = String((char*)configLineBuffer);
            
            // 处理这一行
            processConfigLine(line);
            
            // 清空行缓冲区
            configLineBufferPos = 0;
            
            // 重新获取锁继续读取
            if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(1)) != pdTRUE) {
                return;
            }
        }
        // 行缓冲区满，强制处理
        else if (configLineBufferPos >= CONFIG_LINE_BUFFER_SIZE - 1) {
            xSemaphoreGive(bufferMutex);
            
            configLineBuffer[configLineBufferPos] = 0;
            String line = String((char*)configLineBuffer);
            processConfigLine(line);
            configLineBufferPos = 0;
            
            if (xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(1)) != pdTRUE) {
                return;
            }
        }
    }
    
    xSemaphoreGive(bufferMutex);
}

// 处理一行配置数据
void BluetoothConfig::processConfigLine(const String& line) {
    String trimmedLine = line;
    trimmedLine.trim();
    
    if (trimmedLine.length() == 0) {
        return;
    }
    
    // 过滤BLE数据包标识
    if (trimmedLine.startsWith("BLE DATA") || trimmedLine.startsWith("+RECEIVED")) {
        return; // 忽略BLE数据包的头部和尾部
    }
    
    // 过滤掉只包含不可打印字符的数据
    bool hasText = false;
    for (int i = 0; i < trimmedLine.length(); i++) {
        char c = trimmedLine.charAt(i);
        if (c >= 32 && c <= 126) {
            hasText = true;
            break;
        }
    }
    if (!hasText) {
        return;  // 忽略纯二进制数据
    }
    
    // 检测是否是有效的扫描结果行（数字开头，包含MAC地址格式）
    // 格式：N MAC -RSSI Name
    bool isValidScanResult = false;
    if (trimmedLine.length() > 0) {
        char firstChar = trimmedLine.charAt(0);
        // 扫描结果以数字开头，且包含MAC地址（包含冒号）
        if (firstChar >= '0' && firstChar <= '9' && trimmedLine.indexOf(':') > 0) {
            isValidScanResult = true;
        }
    }
    
    // 累积扫描结果
    if (isScanning && isValidScanResult) {
        scanResultBuffer += trimmedLine + "\n";
        lastScanDataTime = millis();  // 更新最后接收数据时间
        Serial0.printf("[BluetoothConfig] Scan data: %s\n", trimmedLine.c_str());
    }
    
    // 注意：连接/断开检测现在主要通过帧数变化来判断（checkConnectionByFrameCount）
    // 以下代码作为备用，记录蓝牙模块的通知信息
    
    // 检测连接事件："MAC CONNECTD"（仅记录日志）
    int connectedPos = trimmedLine.indexOf("CONNECTD");
    if (connectedPos > 0) {
        // 提取MAC地址
        String mac = trimmedLine.substring(0, connectedPos);
        mac.trim();
        mac.toUpperCase();
        
        // 查找对应的设备
        for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
            String deviceMac = String(devices[i].macAddress);
            deviceMac.toUpperCase();
            if (mac.indexOf(deviceMac) >= 0 || deviceMac.indexOf(mac) >= 0) {
                Serial0.printf("[BluetoothConfig] BT module reports: Device %d (%s) CONNECTD\n", 
                              i + 1, devices[i].macAddress);
                // 不再直接更新状态，让帧数检测来处理
                break;
            }
        }
    }
    
    // 检测断开事件："MAC DISCONNECTD"（仅记录日志）
    int disconnectedPos = trimmedLine.indexOf("DISCONNECTD");
    if (disconnectedPos > 0) {
        // 提取MAC地址
        String mac = trimmedLine.substring(0, disconnectedPos);
        mac.trim();
        mac.toUpperCase();
        
        // 查找对应的设备
        for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
            String deviceMac = String(devices[i].macAddress);
            deviceMac.toUpperCase();
            if (mac.indexOf(deviceMac) >= 0 || deviceMac.indexOf(mac) >= 0) {
                Serial0.printf("[BluetoothConfig] BT module reports: Device %d (%s) DISCONNECTD\n", 
                              i + 1, devices[i].macAddress);
                // 不再直接更新状态，让帧数检测来处理
                break;
            }
        }
    }
    
    // 检测AT响应
    if (trimmedLine.startsWith("+OK")) {
        Serial0.printf("[BluetoothConfig] AT Response: OK\n");
    }
}

bool BluetoothConfig::isBleDataPacket(const uint8_t* data, size_t length) {
    // 检查是否包含BLE数据包的头部和尾部
    if (length < BLE_PACKET_SIZE) {
        return false;
    }
    
    // 检查头部
    bool hasHeader = false;
    for (size_t i = 0; i <= length - sizeof(BLE_DATA_HEADER); i++) {
        if (memcmp(&data[i], BLE_DATA_HEADER, sizeof(BLE_DATA_HEADER)) == 0) {
            hasHeader = true;
            break;
        }
    }
    
    // 检查尾部
    bool hasFooter = false;
    for (size_t i = 0; i <= length - sizeof(BLE_DATA_FOOTER); i++) {
        if (memcmp(&data[i], BLE_DATA_FOOTER, sizeof(BLE_DATA_FOOTER)) == 0) {
            hasFooter = true;
            break;
        }
    }
    
    return hasHeader && hasFooter;
}

// ========== 自动连接流程 ==========

// 自动连接主流程
void BluetoothConfig::autoConnectProcess() {
    uint32_t now = millis();
    
    switch (autoConnectState) {
        case AutoConnectState::IDLE:
            // 等待开机延迟
            if (now - systemStartTime >= AUTO_CONNECT_START_DELAY_MS) {
                Serial0.printf("[BluetoothConfig] ========== Auto Connect Started ==========\n");
                Serial0.printf("[BluetoothConfig] Start delay: %u ms elapsed\n", now - systemStartTime);
                autoConnectState = AutoConnectState::WAITING;
                lastAutoScanTime = now;
                autoScanCount = 0;
            }
            break;
            
        case AutoConnectState::WAITING:
            // 等待扫描间隔
            if (!isScanning && (now - lastAutoScanTime >= AUTO_SCAN_INTERVAL_MS)) {
                if (autoScanCount < MAX_AUTO_SCAN_COUNT) {
                    autoScanCount++;
                    lastAutoScanTime = now;
                    Serial0.printf("[BluetoothConfig] Auto scan #%d/%d\n", autoScanCount, MAX_AUTO_SCAN_COUNT);
                    startScan();
                    autoConnectState = AutoConnectState::SCANNING;
                } else {
                    Serial0.printf("[BluetoothConfig] Auto scan limit reached (%d scans)\n", MAX_AUTO_SCAN_COUNT);
                    autoConnectState = AutoConnectState::COMPLETED;
                }
            }
            break;
            
        case AutoConnectState::SCANNING:
            // 等待扫描完成
            if (!isScanning) {
                // 扫描完成，检查是否扫到所有设备
                if (allTargetDevicesScanned()) {
                    Serial0.printf("[BluetoothConfig] All target devices scanned! Starting connection...\n");
                    startConnectingDevices();
                    autoConnectState = AutoConnectState::CONNECTING;
                } else {
                    // 未扫到所有设备，继续等待下次扫描
                    uint8_t scannedCount = 0;
                    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
                        if (devices[i].scanIndex >= 0) scannedCount++;
                    }
                    Serial0.printf("[BluetoothConfig] Scanned %d/%d devices, will retry\n", scannedCount, DEVICE_COUNT);
                    autoConnectState = AutoConnectState::WAITING;
                }
            }
            break;
            
        case AutoConnectState::CONNECTING:
            // 处理连接流程
            processConnecting();
            break;
            
        case AutoConnectState::COMPLETED:
            // 自动连接流程已完成，不再处理
            break;
    }
}

// 检查是否扫到所有目标设备
bool BluetoothConfig::allTargetDevicesScanned() {
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        if (devices[i].scanIndex < 0 && devices[i].state != DeviceConnectionState::CONNECTED) {
            return false;
        }
    }
    return true;
}

// 开始连接设备流程
void BluetoothConfig::startConnectingDevices() {
    // 构建待连接设备列表（未连接的设备）
    pendingConnectCount = 0;
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        if (devices[i].state != DeviceConnectionState::CONNECTED && devices[i].scanIndex >= 0) {
            pendingConnectDevices[pendingConnectCount++] = i;
        }
    }
    
    Serial0.printf("[BluetoothConfig] Pending connect devices: %d\n", pendingConnectCount);
    
    // 从第一个设备开始连接
    if (pendingConnectCount > 0) {
        currentConnectingDevice = 0;  // 索引到pendingConnectDevices数组
        connectRetryCount = 0;
        lastConnectAttemptTime = 0;
        connectStartTime = millis();
    } else {
        // 没有需要连接的设备
        autoConnectState = AutoConnectState::COMPLETED;
    }
}

// 处理连接流程
void BluetoothConfig::processConnecting() {
    if (currentConnectingDevice < 0 || currentConnectingDevice >= pendingConnectCount) {
        // 所有设备连接流程完成
        Serial0.printf("[BluetoothConfig] All devices connection process completed\n");
        autoConnectState = AutoConnectState::COMPLETED;
        return;
    }
    
    uint32_t now = millis();
    int8_t deviceIndex = pendingConnectDevices[currentConnectingDevice];
    
    // 检查当前设备是否已连接（通过帧数检测）
    if (devices[deviceIndex].state == DeviceConnectionState::CONNECTED) {
        Serial0.printf("[BluetoothConfig] Device %d connected successfully\n", deviceIndex + 1);
        // 进入下一个设备
        currentConnectingDevice++;
        connectRetryCount = 0;
        lastConnectAttemptTime = 0;
        connectStartTime = millis();
        return;
    }
    
    // 检查连接超时（每个设备最多等待指定时间）
    if (now - connectStartTime > CONNECT_WAIT_TIMEOUT_MS) {
        if (connectRetryCount >= MAX_CONNECT_RETRY_COUNT - 1) {
            Serial0.printf("[BluetoothConfig] Device %d connection failed after %d attempts\n", 
                          deviceIndex + 1, MAX_CONNECT_RETRY_COUNT);
            // 放弃当前设备，进入下一个
            currentConnectingDevice++;
            connectRetryCount = 0;
            lastConnectAttemptTime = 0;
            connectStartTime = millis();
            return;
        }
    }
    
    // 检查是否需要发送连接命令
    if (now - lastConnectAttemptTime >= CONNECT_RETRY_INTERVAL_MS) {
        if (connectRetryCount < MAX_CONNECT_RETRY_COUNT) {
            connectRetryCount++;
            lastConnectAttemptTime = now;
            Serial0.printf("[BluetoothConfig] Connecting device %d (attempt %d/%d)...\n", 
                          deviceIndex + 1, connectRetryCount, MAX_CONNECT_RETRY_COUNT);
            connectDevice(deviceIndex);
        }
    }
}

void BluetoothConfig::startScan() {
    if (isScanning) {
        Serial0.printf("[BluetoothConfig] Already scanning, ignore\n");
        return;
    }
    
    Serial0.printf("[BluetoothConfig] Starting scan...\n");
    
    // 重置所有设备的扫描索引
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        devices[i].scanIndex = -1;
        if (devices[i].state != DeviceConnectionState::CONNECTED) {
            updateDeviceState(i, DeviceConnectionState::SCANNING);
        }
    }
    
    // 发送扫描命令
    sendATCommand("AT+SCAN=1,5,1");
    
    isScanning = true;
    scanStartTime = millis();
    lastScanDataTime = 0;  // 重置最后接收数据时间
    scanResultBuffer = "";
}

void BluetoothConfig::processScanResult(const String& result) {
    Serial0.printf("[BluetoothConfig] Processing scan result:\n%s\n", result.c_str());
    
    // 按行分割扫描结果
    int startPos = 0;
    int endPos = 0;
    
    while ((endPos = result.indexOf('\n', startPos)) != -1) {
        String line = result.substring(startPos, endPos);
        line.trim();
        
        if (line.length() > 0) {
            // 解析每一行："0 MAC -RSSI Name"
            String mac;
            int index;
            if (parseMAC(line, mac, index)) {
                // 检查是否是目标设备
                mac.toUpperCase();
                for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
                    String deviceMac = String(devices[i].macAddress);
                    deviceMac.toUpperCase();
                    
                    if (mac.indexOf(deviceMac) >= 0) {
                        devices[i].scanIndex = index;
                        if (devices[i].state != DeviceConnectionState::CONNECTED) {
                            updateDeviceState(i, DeviceConnectionState::SCANNED);
                        }
                        Serial0.printf("[BluetoothConfig] Device %d (%s) found at index %d\n", 
                                      i + 1, devices[i].macAddress, index);
                        break;
                    }
                }
            }
        }
        
        startPos = endPos + 1;
    }
    
    // 更新未扫描到的设备状态
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        if (devices[i].scanIndex == -1 && devices[i].state == DeviceConnectionState::SCANNING) {
            updateDeviceState(i, DeviceConnectionState::DISCONNECTED);
            Serial0.printf("[BluetoothConfig] Device %d (%s) not found\n", 
                          i + 1, devices[i].macAddress);
        }
    }
}

void BluetoothConfig::connectDevice(uint8_t deviceIndex) {
    if (deviceIndex >= DEVICE_COUNT) return;
    
    BluetoothDevice& device = devices[deviceIndex];
    
    if (device.scanIndex < 0) {
        Serial0.printf("[BluetoothConfig] Device %d not scanned, cannot connect\n", deviceIndex + 1);
        return;
    }
    
    if (device.state == DeviceConnectionState::CONNECTED) {
        Serial0.printf("[BluetoothConfig] Device %d already connected\n", deviceIndex + 1);
        return;
    }
    
    Serial0.printf("[BluetoothConfig] Connecting to device %d (index %d)...\n", 
                  deviceIndex + 1, device.scanIndex);
    
    // 发送连接命令
    String cmd = "AT+CONNECT=" + String(device.scanIndex);
    sendATCommand(cmd);
    
    updateDeviceState(deviceIndex, DeviceConnectionState::CONNECTING);
}

void BluetoothConfig::updateDeviceState(uint8_t deviceIndex, DeviceConnectionState newState) {
    if (deviceIndex >= DEVICE_COUNT) return;
    
    devices[deviceIndex].state = newState;
    devices[deviceIndex].lastUpdateTime = millis();
}

void BluetoothConfig::updateLEDByDeviceState(uint8_t deviceIndex) {
    if (deviceIndex >= DEVICE_COUNT) return;
    
    LEDState ledState;
    uint8_t sensorId = deviceIndex + 1;  // 设备索引对应传感器ID（1-3）
    
    switch (devices[deviceIndex].state) {
        case DeviceConnectionState::DISCONNECTED:
        case DeviceConnectionState::SCANNING:
        case DeviceConnectionState::CONNECTING:
            ledState = LEDState::OFF;
            break;
            
        case DeviceConnectionState::SCANNED:
            // 被扫描到：慢闪
            ledState = LEDState::SLOW_BLINK;
            break;
            
        case DeviceConnectionState::CONNECTED:
            // 已连接，需要检查时间同步状态
            if (timeSync) {
                // 检查是否正在校准
                if (timeSync->isSensorCalibrating(sensorId)) {
                    // 正在校准：快闪
                    ledState = LEDState::FAST_BLINK;
                } else if (timeSync->isTimeSyncReady(sensorId)) {
                    // 校准完成：常亮
                    ledState = LEDState::ON;
                } else {
                    // 已连接但未校准：快闪（等待校准）
                    ledState = LEDState::FAST_BLINK;
                }
            } else {
                // 没有TimeSync实例，默认常亮
                ledState = LEDState::ON;
            }
            break;
            
        default:
            ledState = LEDState::OFF;
            break;
    }
    
    // 只在状态改变时更新LED
    if (leds[deviceIndex].state != ledState) {
        setLEDState(deviceIndex, ledState);
    }
}

void BluetoothConfig::handleButtonPressForDevice(uint8_t deviceIndex) {
    if (deviceIndex >= DEVICE_COUNT) return;
    
    BluetoothDevice& device = devices[deviceIndex];
    uint8_t sensorId = deviceIndex + 1;  // 设备索引对应传感器ID（1-3）
    
    switch (device.state) {
        case DeviceConnectionState::DISCONNECTED:
            // 未连接状态，按下按钮进行扫描
            Serial0.printf("[BluetoothConfig] Button %d: Triggering scan for device %d\n", 
                          deviceIndex + 1, deviceIndex + 1);
            startScan();
            break;
            
        case DeviceConnectionState::SCANNED:
            // 已扫描到，按下按钮进行连接
            Serial0.printf("[BluetoothConfig] Button %d: Connecting device %d\n", 
                          deviceIndex + 1, deviceIndex + 1);
            connectDevice(deviceIndex);
            break;
            
        case DeviceConnectionState::CONNECTED:
            // 已连接，按下按钮进行单次校准
            if (timeSync) {
                // 检查是否正在校准
                if (timeSync->isSensorCalibrating(sensorId)) {
                    Serial0.printf("[BluetoothConfig] Button %d: Sensor %d is already calibrating, please wait\n", 
                                  deviceIndex + 1, sensorId);
                } else {
                    Serial0.printf("[BluetoothConfig] Button %d: Starting single calibration for sensor %d\n", 
                                  deviceIndex + 1, sensorId);
                    timeSync->startSingleSensorCalibration(sensorId);
                }
            } else {
                Serial0.printf("[BluetoothConfig] Button %d: TimeSync not available\n", deviceIndex + 1);
            }
            break;
            
        case DeviceConnectionState::SCANNING:
        case DeviceConnectionState::CONNECTING:
            // 扫描或连接中，不响应
            Serial0.printf("[BluetoothConfig] Button %d: Device %d in state %d, please wait\n", 
                          deviceIndex + 1, deviceIndex + 1, (int)device.state);
            break;
    }
}

void BluetoothConfig::sendATCommand(const String& command) {
    String fullCmd = command + "\r\n";
    uart_write_bytes(UART_NUM_1, fullCmd.c_str(), fullCmd.length());
    Serial0.printf("[BluetoothConfig] Sent AT command: %s\n", command.c_str());
}

bool BluetoothConfig::parseMAC(const String& line, String& mac, int& index) {
    // 解析格式："0 MAC -RSSI Name"
    // 例如："4 BB:DD:E9:09:67:00 -28 CDEBYTE_BLE/r/n"
    
    int firstSpace = line.indexOf(' ');
    if (firstSpace < 0) return false;
    
    // 提取索引
    String indexStr = line.substring(0, firstSpace);
    indexStr.trim();
    // 去除引号
    indexStr.replace("\"", "");
    index = indexStr.toInt();
    
    int secondSpace = line.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) return false;
    
    // 提取MAC地址
    mac = line.substring(firstSpace + 1, secondSpace);
    mac.trim();
    mac.replace("\"", "");
    
    // MAC地址应该包含冒号
    if (mac.indexOf(':') < 0) return false;
    
    return true;
}

// 检查是否所有设备都已连接
bool BluetoothConfig::areAllDevicesConnected() const {
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        if (devices[i].state != DeviceConnectionState::CONNECTED) {
            return false;
        }
    }
    return true;
}

// 获取已连接设备的数量
uint8_t BluetoothConfig::getConnectedDeviceCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        if (devices[i].state == DeviceConnectionState::CONNECTED) {
            count++;
        }
    }
    return count;
}

