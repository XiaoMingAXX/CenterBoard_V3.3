#include "BluetoothConfig.h"

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
    bleBufferPos = 0;
    memset(bleBuffer, 0, sizeof(bleBuffer));
    
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
    
    // 初始化自动连接
    autoConnectStarted = false;
    systemStartTime = millis();
    lastAutoScanTime = 0;
    autoScanCount = 0;
    
    // 初始化连接队列
    connectQueueSize = 0;
    memset(connectQueue, -1, sizeof(connectQueue));
}

BluetoothConfig::~BluetoothConfig() {
    // 清理资源
}

bool BluetoothConfig::initialize() {
    Serial0.printf("[BluetoothConfig] Initializing...\n");
    
    // 初始化GPIO
    initGPIO();
    
    Serial0.printf("[BluetoothConfig] Initialized successfully\n");
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
    // 处理按钮和LED
    handleButtonsAndLEDs();
    
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
    
    // 清空缓冲区
    bleBufferPos = 0;
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

void BluetoothConfig::forwardUartData(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return;
    }
    
    // 配置模式下，将UART数据（AT响应）转发到Serial0
    if (configMode) {
        Serial0.write(data, length);
    } else {
        // 非配置模式下，处理蓝牙事件（连接/断开通知、扫描结果等）
        String dataStr = "";
        for (size_t i = 0; i < length; i++) {
            dataStr += (char)data[i];
        }
        processBluetoothEvent(dataStr);
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
    
    // 1. 自动连接逻辑：启动10秒后开始，每隔3秒扫描一次，最多5次
    if (!autoConnectStarted && (now - systemStartTime >= AUTO_CONNECT_START_DELAY_MS)) {
        autoConnectStarted = true;
        autoScanCount = 0;
        lastAutoScanTime = now;
        Serial0.printf("[BluetoothConfig] Auto connect started after 10 seconds\n");
        Serial0.printf("[BluetoothConfig] Will scan every 3 seconds, max 5 times\n");
        startScan();
        autoScanCount++;
    }
    
    // 如果已开始自动连接流程，检查是否需要继续扫描
    if (autoConnectStarted && !isScanning && autoScanCount < MAX_AUTO_SCAN_COUNT) {
        // 检查所有设备是否都已连接
        bool allConnected = true;
        for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
            if (devices[i].state != DeviceConnectionState::CONNECTED) {
                allConnected = false;
                break;
            }
        }
        
        // 如果还有设备未连接，且距离上次扫描超过3秒，则继续扫描
        if (!allConnected && (now - lastAutoScanTime >= AUTO_SCAN_INTERVAL_MS)) {
            lastAutoScanTime = now;
            Serial0.printf("[BluetoothConfig] Auto scan #%d\n", autoScanCount + 1);
            startScan();
            autoScanCount++;
        }
        
        // 如果已达到最大扫描次数
        if (autoScanCount >= MAX_AUTO_SCAN_COUNT) {
            Serial0.printf("[BluetoothConfig] Auto scan completed (%d scans)\n", autoScanCount);
        }
    }
    
    // 2. 检查扫描是否完成
    if (isScanning) {
        bool scanEnded = false;
        
        // 方式1：如果距离最后接收扫描数据超过500ms，认为扫描完成
        if (lastScanDataTime > 0 && (now - lastScanDataTime >= SCAN_DATA_TIMEOUT_MS)) {
            scanEnded = true;
            Serial0.printf("[BluetoothConfig] Scan completed (no more data)\n");
        }
        // 方式2：绝对超时保护（3秒）
        else if (now - scanStartTime >= SCAN_TIMEOUT_MS) {
            scanEnded = true;
            Serial0.printf("[BluetoothConfig] Scan timeout\n");
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
                    Serial0.printf("[BluetoothConfig] Device %d reset to DISCONNECTED\n", i + 1);
                }
            }
            
            // 扫描完成后，检查是否所有3个设备都被扫描到
            bool allScanned = true;
            for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
                if (devices[i].state != DeviceConnectionState::SCANNED && 
                    devices[i].state != DeviceConnectionState::CONNECTED) {
                    allScanned = false;
                    break;
                }
            }
            
            // 只有当3个设备都被扫描到时才连接
            if (allScanned) {
                Serial0.printf("[BluetoothConfig] All 3 devices scanned, connecting...\n");
                for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
                    if (devices[i].state == DeviceConnectionState::SCANNED) {
                        connectDevice(i);
                        delay(100);  // 连接命令之间稍作延迟
                    }
                }
            } else {
                Serial0.printf("[BluetoothConfig] Not all devices scanned, will retry later\n");
                // 统计扫描到的设备数
                uint8_t scannedCount = 0;
                for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
                    if (devices[i].state == DeviceConnectionState::SCANNED || 
                        devices[i].state == DeviceConnectionState::CONNECTED) {
                        scannedCount++;
                    }
                }
                Serial0.printf("[BluetoothConfig] Scanned devices: %d/%d\n", scannedCount, DEVICE_COUNT);
            }
        }
    }
    
    // 3. 更新所有设备的LED状态
    for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
        updateLEDByDeviceState(i);
    }
}

void BluetoothConfig::processBluetoothEvent(const String& data) {
    // 累积扫描结果
    if (isScanning) {
        scanResultBuffer += data;
        lastScanDataTime = millis();  // 更新最后接收数据时间
        
        // 检测扫描结果：包含MAC地址格式的行（包含冒号）和引号
        // 扫描结果格式："N MAC -RSSI Name\r\n"
        // 当接收到包含MAC地址的行时，说明扫描结果正在返回
        if (data.indexOf(':') >= 0 && data.indexOf('"') >= 0) {
            Serial0.printf("[BluetoothConfig] Scan data received\n");
        }
    }
    
    // 检测连接事件："MAC CONNECTD"
    int connectedPos = data.indexOf("CONNECTD");
    if (connectedPos > 0) {
        // 提取MAC地址
        String mac = data.substring(0, connectedPos);
        mac.trim();
        mac.toUpperCase();
        
        // 查找对应的设备
        for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
            String deviceMac = String(devices[i].macAddress);
            deviceMac.toUpperCase();
            if (mac.indexOf(deviceMac) >= 0 || deviceMac.indexOf(mac) >= 0) {
                updateDeviceState(i, DeviceConnectionState::CONNECTED);
                Serial0.printf("[BluetoothConfig] Device %d (%s) CONNECTED\n", 
                              i + 1, devices[i].macAddress);
                break;
            }
        }
    }
    
    // 检测断开事件："MAC DISCONNECTD"
    int disconnectedPos = data.indexOf("DISCONNECTD");
    if (disconnectedPos > 0) {
        // 提取MAC地址
        String mac = data.substring(0, disconnectedPos);
        mac.trim();
        mac.toUpperCase();
        
        // 查找对应的设备
        for (uint8_t i = 0; i < DEVICE_COUNT; i++) {
            String deviceMac = String(devices[i].macAddress);
            deviceMac.toUpperCase();
            if (mac.indexOf(deviceMac) >= 0 || deviceMac.indexOf(mac) >= 0) {
                updateDeviceState(i, DeviceConnectionState::DISCONNECTED);
                Serial0.printf("[BluetoothConfig] Device %d (%s) DISCONNECTED\n", 
                              i + 1, devices[i].macAddress);
                break;
            }
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
    sendATCommand("AT+SCAN=1,2,1");
    
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
    
    switch (devices[deviceIndex].state) {
        case DeviceConnectionState::DISCONNECTED:
            ledState = LEDState::OFF;
            break;
        case DeviceConnectionState::SCANNING:
            ledState = LEDState::SLOW_BLINK;
            break;
        case DeviceConnectionState::SCANNED:
        case DeviceConnectionState::CONNECTING:
            ledState = LEDState::FAST_BLINK;
            break;
        case DeviceConnectionState::CONNECTED:
            ledState = LEDState::ON;
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
    
    switch (device.state) {
        case DeviceConnectionState::DISCONNECTED:
            // 未连接状态，按下按钮进行扫描
            Serial0.printf("[BluetoothConfig] Button %d: Triggering scan for device %d\n", 
                          deviceIndex + 1, deviceIndex + 1);
            startScan();
            break;
            
        case DeviceConnectionState::SCANNED:
        case DeviceConnectionState::CONNECTING:
            // 已扫描到，按下按钮进行连接
            Serial0.printf("[BluetoothConfig] Button %d: Connecting device %d\n", 
                          deviceIndex + 1, deviceIndex + 1);
            connectDevice(deviceIndex);
            break;
            
        case DeviceConnectionState::SCANNING:
        
        case DeviceConnectionState::CONNECTED:
            // 其他状态不响应
            Serial0.printf("[BluetoothConfig] Button %d: Device %d in state %d, no action\n", 
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

