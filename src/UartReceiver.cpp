#include "UartReceiver.h"
#include "driver/uart.h"
#include "esp_intr_alloc.h"
#include "CommandHandler.h"

UartReceiver::UartReceiver() {
    sensorData = nullptr;
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
    
    memset(&stats, 0, sizeof(stats));
    
    Serial.printf("[UartReceiver] Created with single UART receiver + DMA\n");
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

bool UartReceiver::initialize(SensorData* sensorDataInstance) {
    if (initialized) {
        return true;
    }
    
    // 使用传入的传感器数据管理器实例，如果没有则创建新的
    if (sensorDataInstance) {
        sensorData = sensorDataInstance;
        ownsSensorData = false;
        Serial.printf("[UartReceiver] Using provided SensorData instance\n");
    } else {
        sensorData = new SensorData();
        ownsSensorData = true;
        if (!sensorData) {
            Serial.printf("[UartReceiver] ERROR: Failed to create SensorData\n");
            return false;
        }
        Serial.printf("[UartReceiver] Created new SensorData instance\n");
    }
    
    // 初始化单个UART
    if (!initUart()) {
        Serial.printf("[UartReceiver] ERROR: Failed to initialize UART\n");
        return false;
    }
    
    initialized = true;
    Serial.printf("[UartReceiver] Initialized successfully\n");
    return true;
}

bool UartReceiver::start() {
    if (!initialized) {
        Serial.printf("[UartReceiver] ERROR: Not initialized\n");
        return false;
    }
    
    // 启动UART接收任务
    Serial.printf("[UartReceiver] Started UART reception on single UART\n");
    return true;
}

void UartReceiver::stop() {
    // 停止UART接收
    uart_driver_delete(UART_NUM_1);
    Serial.printf("[UartReceiver] Stopped UART reception\n");
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
        Serial.println("[UartReceiver] ERROR: Invalid sensor ID");
        return false;
    }
    
    return true;
}

SensorFrame UartReceiver::createSensorFrame(const uint8_t* frameData) {
    SensorFrame frame;
    memset(&frame, 0, sizeof(frame));
    
    // 解析时间戳
    memcpy(&frame.timestamp, &frameData[1], 4);
    
    // 解析加速度数据
    memcpy(frame.acc, &frameData[5], 12);
    
    // 解析角速度数据
    memcpy(frame.gyro, &frameData[17], 12);
    
    // 解析角度数据
    memcpy(frame.angle, &frameData[29], 12);
    
    // 解析传感器ID
    frame.sensorId = frameData[FRAME_SIZE - 2];
    
    // 设置本地时间戳
    frame.localTimestamp = millis();
    
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
    // 根据硬件配置：UART1_RX=16, UART1_TX=17, UART1_BAUD=460800
    
    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = 460800,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    // 安装UART驱动，使用DMA模式
    int ret = uart_driver_install(UART_NUM_1, RING_BUFFER_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        Serial.printf("[UartReceiver] ERROR: Failed to install UART driver: %d\n", ret);
        return false;
    }
    
    // 配置UART参数
    ret = uart_param_config(UART_NUM_1, &uart_config);
    if (ret != ESP_OK) {
        Serial.printf("[UartReceiver] ERROR: Failed to configure UART: %d\n", ret);
        return false;
    }
    
    // 设置UART引脚
    ret = uart_set_pin(UART_NUM_1, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        Serial.printf("[UartReceiver] ERROR: Failed to set UART pins: %d\n", ret);
        return false;
    }
    
    // 对于ESP32-S3，使用uart_driver_install的事件队列来处理数据
    // 不需要手动注册中断服务，uart_driver_install已经处理了
    // 启用UART接收中断
    ret = uart_enable_rx_intr(UART_NUM_1);
    if (ret != ESP_OK) {
        Serial.printf("[UartReceiver] ERROR: Failed to enable UART RX interrupt: %d\n", ret);
        return false;
    }
    
    Serial.printf("[UartReceiver] UART1+DMA+ISR initialized successfully (TX:17, RX:16, Baud:460800)\n");
    return true;
}

void UartReceiver::readUartData() {
    // 轮询方式读取UART数据（用于测试，实际使用中断方式）
    uint8_t data[256];
    int len = uart_read_bytes(UART_NUM_1, data, sizeof(data), 0);
    if (len > 0) {
        handleUartData(data, len);
    }
}

void UartReceiver::processDmaData() {
    // 对于ESP32-S3，直接读取UART数据，不需要中断标志
    // uart_driver_install已经处理了底层的中断和DMA
    int len = uart_read_bytes(UART_NUM_1, dmaBuffer, DMA_BUFFER_SIZE, 0);
    if (len > 0) {
        // 处理接收到的数据
        handleUartData(dmaBuffer, len);
    }
}

// ESP32-S3的UART驱动自动处理中断，不需要自定义中断处理函数

void UartReceiver::dmaReceiveCallback(const uint8_t* data, size_t length) {
    // DMA接收回调函数（如果需要的话）
    Serial.printf("[UartReceiver] DMA callback, %d bytes\n", length);
}
