#ifndef UART_RECEIVER_H
#define UART_RECEIVER_H

#include <Arduino.h>
#include "RingBuffer.h"
#include "SensorData.h"

// UART接收器类，处理单个串口的DMA接收，通过ID区分传感器
class UartReceiver {
public:
    UartReceiver();
    ~UartReceiver();
    
    // 初始化UART和DMA
    bool initialize(SensorData* sensorDataInstance = nullptr);
    
    // 启动接收
    bool start();
    
    // 停止接收
    void stop();
    
    // 处理接收到的数据（在中断中调用）
    void handleUartData(const uint8_t* data, size_t length);
    
    // 读取UART数据（轮询方式，用于测试）
    void readUartData();
    
    // 处理DMA完成的数据
    void processDmaData();
    
    // ESP32-S3的UART驱动自动处理中断，不需要自定义中断处理函数
    
    // DMA缓冲区
    static const size_t DMA_BUFFER_SIZE = 1024;
    uint8_t dmaBuffer[DMA_BUFFER_SIZE];
    
    // 获取统计信息
    struct Stats {
        uint32_t totalBytesReceived;
        uint32_t totalFramesParsed;
        uint32_t parseErrors;
        uint32_t sensorFrameCounts[4]; // 每个传感器的帧数统计
    };
    Stats getStats() const;
    
    // 重置统计信息
    void resetStats();
    
private:
    static const size_t RING_BUFFER_SIZE = 8192; // 增大缓冲区以处理多传感器数据
    static const size_t FRAME_SIZE = 43; // 帧头(1) + 时间戳(4) + 加速度(12) + 角速度(12) + 角度(12) + ID(1) + 帧尾(1)
    
    RingBuffer* ringBuffer; // 单个UART的环形缓冲区
    SensorData* sensorData;
    bool ownsSensorData; // 标记是否拥有SensorData实例
    SemaphoreHandle_t mutex;
    Stats stats;
    bool initialized;
    
    // 帧解析相关
    struct FrameParser {
        uint8_t buffer[FRAME_SIZE];
        uint8_t pos;
        bool inFrame;
    };
    FrameParser parser;
    
    // 解析帧数据
    bool parseFrame(const uint8_t* frameData);
    
    // 验证帧格式
    bool validateFrame(const uint8_t* frameData);
    
    // 创建传感器帧
    SensorFrame createSensorFrame(const uint8_t* frameData);
    
    // 处理接收到的字节
    void processByte(uint8_t byte);
    
    // 初始化UART
    bool initUart();
    
    // DMA接收回调（占位符）
    static void dmaReceiveCallback(const uint8_t* data, size_t length);
};

#endif // UART_RECEIVER_H
