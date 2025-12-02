#include "UartReceiver.h"
#include "driver/uart.h"
#include "esp_intr_alloc.h"
#include "CommandHandler.h"
#include "TimeSync.h"
#include "BluetoothConfig.h"

// 定义蓝牙数据包特征
const uint8_t UartReceiver::BLE_DATA_HEADER[10] = {
    0x42, 0x4C, 0x45, 0x20, 0x44, 0x41, 0x54, 0x41, 0x0D, 0x0A  // BLE DATA\r\n
};

const uint8_t UartReceiver::BLE_DATA_FOOTER[16] = {
    0x2B, 0x52, 0x45, 0x43, 0x45, 0x49, 0x56, 0x45, 
    0x44, 0x3A, 0x31, 0x2C, 0x34, 0x33, 0x0D, 0x0A  // +RECEIVED:1,43\r\n
};

UartReceiver::UartReceiver() {
    sensorData = nullptr;
    timeSync = nullptr;
    bluetoothConfig = nullptr;
    ownsSensorData = false;
    ringBuffer = nullptr;
    mutex = xSemaphoreCreateMutex();
    initialized = false;
    // ESP32-S3的UART驱动自动处理中断，不需要标志
    
    // 初始化环形缓冲区
    ringBuffer = new RingBuffer(RING_BUFFER_SIZE);
    memset(&parser, 0, sizeof(FrameParser));
    
    // 初始化DMA缓冲区
    memset(dmaBuffer, 0, DMA_BUFFER_SIZE);
    
    // 初始化蓝牙数据包解析器
    resetBleParser();
    
    memset(&stats, 0, sizeof(stats));
    
    
    Serial0.printf("[UartReceiver] Created with single UART receiver + DMA\n");
}

UartReceiver::~UartReceiver() {
    stop();
    
    if (ringBuffer) {
        delete ringBuffer;
    }
    
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    
    if (sensorData && ownsSensorData) {
        delete sensorData;
    }
}

bool UartReceiver::initialize(SensorData* sensorDataInstance, TimeSync* timeSyncInstance) {
    if (initialized) {
        return true;
    }
    
    // 使用传入的传感器数据管理器实例，如果没有则创建新的
    if (sensorDataInstance) {
        sensorData = sensorDataInstance;
        ownsSensorData = false;
        Serial0.printf("[UartReceiver] Using provided SensorData instance\n");
    } else {
        sensorData = new SensorData();
        ownsSensorData = true;
        if (!sensorData) {
            Serial0.printf("[UartReceiver] ERROR: Failed to create SensorData\n");
            return false;
        }
        Serial0.printf("[UartReceiver] Created new SensorData instance\n");
    }
    
    // 设置时间同步实例
    timeSync = timeSyncInstance;
    if (timeSync) {
        Serial0.printf("[UartReceiver] Using provided TimeSync instance\n");
    } else {
        Serial0.printf("[UartReceiver] WARNING: No TimeSync instance provided\n");
    }
    
    // 初始化单个UART
    if (!initUart()) {
        Serial0.printf("[UartReceiver] ERROR: Failed to initialize UART\n");
        return false;
    }
    
    initialized = true;
    Serial0.printf("[UartReceiver] Initialized successfully\n");
    return true;
}

bool UartReceiver::start() {
    if (!initialized) {
        Serial0.printf("[UartReceiver] ERROR: Not initialized\n");
        return false;
    }
    
    // 启动UART接收任务
    Serial0.printf("[UartReceiver] Started UART reception on single UART\n");
    return true;
}

void UartReceiver::stop() {
    // 刷新配置缓冲区
    flushConfigBuffer();
    
    // 停止UART接收
    uart_driver_delete(UART_NUM_1);
    Serial0.printf("[UartReceiver] Stopped UART reception\n");
}

void UartReceiver::handleUartData(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return;
    }
    
    // 直接处理接收到的数据，避免不必要的复制
    // 直接从DMA缓冲区处理字节，无需写入环形缓冲区
    for (size_t i = 0; i < length; i++) {
        processByte(data[i]);
    }
    
    // 更新统计信息
    stats.totalBytesReceived += length;
}

UartReceiver::Stats UartReceiver::getStats() const {
    return stats;
}

void UartReceiver::resetStats() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        memset(&stats, 0, sizeof(stats));
        xSemaphoreGive(mutex);
    }
}

bool UartReceiver::parseFrame(const uint8_t* frameData) {
    if (!validateFrame(frameData)) {
        stats.parseErrors++;
        return false;
    }
    
    SensorFrame frame = createSensorFrame(frameData);
    if (sensorData->addFrame(frame)) {
        stats.totalFramesParsed++;
        // 统计每个传感器的帧数
        if (frame.sensorId >= 1 && frame.sensorId <= 4) {
            stats.sensorFrameCounts[frame.sensorId - 1]++;
        }
        
        // 显示实时数据（如果启用）
        CommandHandler::displayRealtimeSensorData(frame);
        
        return true;
    }
    
    return false;
}

bool UartReceiver::validateFrame(const uint8_t* frameData) {
    // 检查帧头
    if (frameData[0] != 0xAA) {
        return false;
    }
    
    // 检查帧尾
    if (frameData[FRAME_SIZE - 1] != 0x55) {
        return false;
    }
    
    // 检查传感器ID
    uint8_t sensorId = frameData[FRAME_SIZE - 2];
    if (sensorId < 1 || sensorId > 4) {
        Serial0.println("[UartReceiver] ERROR: Invalid sensor ID");
        return false;
    }
    
    return true;
}

SensorFrame UartReceiver::createSensorFrame(const uint8_t* frameData) {
    SensorFrame frame;
    memset(&frame, 0, sizeof(frame));
    
    // 解析传感器时间戳S（毫秒）
    memcpy(&frame.timestamp, &frameData[1], 4);
    frame.timestamp-- ;     //减去串口传输延时1ms 43*10/460800=0.0009375s
    
    // 解析传感器ID（需要先解析，因为时间同步需要用到）
    frame.sensorId = frameData[FRAME_SIZE - 2];
    
    // 记录ESP32时间E（微秒精度）
    int64_t espTimeUs = esp_timer_get_time();
    
    // 如果时间同步模块可用，添加时间对到滑动窗口（快速操作）
    if (timeSync) {

        timeSync->addTimePair(frame.sensorId, frame.timestamp, espTimeUs);
        
        // 计算同步后的时间戳（快速操作，不进行拟合计算）
        uint64_t syncedTimestamp = timeSync->calculateTimestamp(frame.sensorId, frame.timestamp);
        
        // 保存原始时间戳
        frame.rawTimestamp = syncedTimestamp;
        
        // 格式化时间戳为时/分/秒/毫秒格式
        frame.timestamp = timeSync->formatTimestamp(syncedTimestamp);
    } else {
        Serial0.printf("[UartReceiver] WARNING: timeSync is null!\n");
    }
    
    // 解析加速度数据
    memcpy(frame.acc, &frameData[5], 12);
    
    // 解析角速度数据
    memcpy(frame.gyro, &frameData[17], 12);
    
    // 解析角度数据
    memcpy(frame.angle, &frameData[29], 12);
    
    // 设置本地时间戳（如果没有时间同步，使用原始时间戳）
    if (!timeSync) {
        frame.rawTimestamp = frame.timestamp;
    }
    
    // 验证数据有效性
    frame.valid = true;
    
    return frame;
}

void UartReceiver::processByte(uint8_t byte) {
    if (!parser.inFrame) {
        // 寻找帧头
        if (byte == 0xAA) {
            parser.buffer[0] = byte;
            parser.pos = 1;
            parser.inFrame = true;
        }
    } else {
        // 接收帧数据
        if (parser.pos < FRAME_SIZE) {
            parser.buffer[parser.pos] = byte;
            parser.pos++;
            
            // 检查是否接收完整帧
            if (parser.pos == FRAME_SIZE) {
                parseFrame(parser.buffer);
                parser.inFrame = false;
                parser.pos = 0;
            }
        } else {
            // 帧长度错误，重置
            parser.inFrame = false;
            parser.pos = 0;
            stats.parseErrors++;
        }
    }
}

bool UartReceiver::initUart() {
    // 实现ESP32-S3 UART+DMA初始化
    // 根据硬件配置：UART1_RX=18, UART1_TX=17, UART1_BAUD=460800
    
    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    // 安装UART驱动，使用DMA模式
    int ret = uart_driver_install(UART_NUM_1, RING_BUFFER_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        Serial0.printf("[UartReceiver] ERROR: Failed to install UART driver: %d\n", ret);
        return false;
    }
    
    // 配置UART参数
    ret = uart_param_config(UART_NUM_1, &uart_config);
    if (ret != ESP_OK) {
        Serial0.printf("[UartReceiver] ERROR: Failed to configure UART: %d\n", ret);
        return false;
    }
    
    // 设置UART引脚：TX=17, RX=18
    ret = uart_set_pin(UART_NUM_1, 17, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        Serial0.printf("[UartReceiver] ERROR: Failed to set UART pins: %d\n", ret);
        return false;
    }
    
    // 对于ESP32-S3，使用uart_driver_install的事件队列来处理数据
    // 不需要手动注册中断服务，uart_driver_install已经处理了
    // 启用UART接收中断
    ret = uart_enable_rx_intr(UART_NUM_1);
    if (ret != ESP_OK) {
        Serial0.printf("[UartReceiver] ERROR: Failed to enable UART RX interrupt: %d\n", ret);
        return false;
    }
    
    Serial0.printf("[UartReceiver] UART1+DMA+ISR initialized successfully (TX:17, RX:18, Baud:460800)\n");
    return true;
}



void UartReceiver::processDmaData() {
    // 对于ESP32-S3，直接读取UART数据，不需要中断标志
    // uart_driver_install已经处理了底层的中断和DMA
    int len = uart_read_bytes(UART_NUM_1, dmaBuffer, DMA_BUFFER_SIZE, 0);
    if (len > 0) {
        // 使用状态机逐字节处理，准确识别蓝牙数据包和配置数据
        for (int i = 0; i < len; i++) {
            processBleStateMachine(dmaBuffer[i]);
        }
        
        // 如果配置缓冲区积累了较多数据且处于IDLE状态，刷新缓冲区
        // 避免配置数据长时间滞留在缓冲区中
        if (bleParser.state == BlePacketState::IDLE && bleParser.configBufferPos > 0) {
            flushConfigBuffer();
        }
    }
}

void UartReceiver::setBluetoothConfig(BluetoothConfig* btConfig) {
    bluetoothConfig = btConfig;
    if (bluetoothConfig) {
        Serial0.printf("[UartReceiver] BluetoothConfig module registered\n");
    }
}

void UartReceiver::resetBleParser() {
    bleParser.state = BlePacketState::IDLE;
    bleParser.headerMatchCount = 0;
    bleParser.dataCount = 0;
    bleParser.footerMatchCount = 0;
    bleParser.configBufferPos = 0;
    memset(bleParser.dataBuffer, 0, sizeof(bleParser.dataBuffer));
    memset(bleParser.configBuffer, 0, sizeof(bleParser.configBuffer));
}

void UartReceiver::processBleStateMachine(uint8_t byte) {
    switch (bleParser.state) {
        case BlePacketState::IDLE:
            // 寻找BLE_DATA_HEADER的第一个字节
            if (byte == BLE_DATA_HEADER[0]) {
                bleParser.state = BlePacketState::IN_HEADER;
                bleParser.headerMatchCount = 1;
            } else {
                // 不是蓝牙包头，作为配置数据缓存
                if (bleParser.configBufferPos < sizeof(bleParser.configBuffer)) {
                    bleParser.configBuffer[bleParser.configBufferPos++] = byte;
                } else {
                    // 配置缓冲区满，先刷新
                    flushConfigBuffer();
                    bleParser.configBuffer[bleParser.configBufferPos++] = byte;
                }
            }
            break;
            
        case BlePacketState::IN_HEADER:
            // 继续匹配头部
            if (byte == BLE_DATA_HEADER[bleParser.headerMatchCount]) {
                bleParser.headerMatchCount++;
                if (bleParser.headerMatchCount == sizeof(BLE_DATA_HEADER)) {
                    // 头部匹配完成，开始接收数据
                    bleParser.state = BlePacketState::IN_DATA;
                    bleParser.dataCount = 0;
                    
                    // 在进入数据接收状态前，先刷新配置缓冲区（如果有的话）
                    if (bleParser.configBufferPos > 0) {
                        flushConfigBuffer();
                    }
                }
            } else {
                // 头部匹配失败，将之前匹配的部分作为配置数据
                for (uint8_t i = 0; i < bleParser.headerMatchCount; i++) {
                    if (bleParser.configBufferPos < sizeof(bleParser.configBuffer)) {
                        bleParser.configBuffer[bleParser.configBufferPos++] = BLE_DATA_HEADER[i];
                    }
                }
                // 重置状态机
                bleParser.state = BlePacketState::IDLE;
                bleParser.headerMatchCount = 0;
                // 重新处理当前字节
                processBleStateMachine(byte);
            }
            break;
            
        case BlePacketState::IN_DATA:
            // 接收43字节数据
            bleParser.dataBuffer[bleParser.dataCount++] = byte;
            if (bleParser.dataCount == BLE_DATA_LENGTH) {
                // 数据接收完成，开始匹配尾部
                bleParser.state = BlePacketState::IN_FOOTER;
                bleParser.footerMatchCount = 0;
            }
            break;
            
        case BlePacketState::IN_FOOTER:
            // 匹配尾部
            if (byte == BLE_DATA_FOOTER[bleParser.footerMatchCount]) {
                bleParser.footerMatchCount++;
                if (bleParser.footerMatchCount == sizeof(BLE_DATA_FOOTER)) {
                    // 完整的蓝牙数据包接收完成
                    bleParser.state = BlePacketState::COMPLETE;
                    handleCompleteBlePacket();
                    // 重置状态机
                    resetBleParser();
                }
            } else {
                // 尾部匹配失败，这不是有效的蓝牙数据包
                // 将头部+数据+已匹配的尾部作为配置数据
                for (uint8_t i = 0; i < sizeof(BLE_DATA_HEADER); i++) {
                    if (bleParser.configBufferPos < sizeof(bleParser.configBuffer)) {
                        bleParser.configBuffer[bleParser.configBufferPos++] = BLE_DATA_HEADER[i];
                    }
                }
                for (uint8_t i = 0; i < BLE_DATA_LENGTH; i++) {
                    if (bleParser.configBufferPos < sizeof(bleParser.configBuffer)) {
                        bleParser.configBuffer[bleParser.configBufferPos++] = bleParser.dataBuffer[i];
                    }
                }
                for (uint8_t i = 0; i < bleParser.footerMatchCount; i++) {
                    if (bleParser.configBufferPos < sizeof(bleParser.configBuffer)) {
                        bleParser.configBuffer[bleParser.configBufferPos++] = BLE_DATA_FOOTER[i];
                    }
                }
                
                // 重置状态机
                bleParser.state = BlePacketState::IDLE;
                bleParser.headerMatchCount = 0;
                bleParser.footerMatchCount = 0;
                // 重新处理当前字节
                processBleStateMachine(byte);
            }
            break;
            
        case BlePacketState::COMPLETE:
            // 不应该到这里，重置状态机
            resetBleParser();
            processBleStateMachine(byte);
            break;
    }
}

void UartReceiver::handleCompleteBlePacket() {
    // 完整的蓝牙透传数据包，处理43字节的传感器数据
    handleUartData(bleParser.dataBuffer, BLE_DATA_LENGTH);
}

void UartReceiver::flushConfigBuffer() {
    // 将配置缓冲区的数据转发给BluetoothConfig模块
    if (bleParser.configBufferPos > 0 && bluetoothConfig) {
        bluetoothConfig->forwardUartData(bleParser.configBuffer, bleParser.configBufferPos);
    }
    bleParser.configBufferPos = 0;
}


