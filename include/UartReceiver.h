#ifndef UART_RECEIVER_H
#define UART_RECEIVER_H

#include <Arduino.h>
#include "RingBuffer.h"
#include "SensorData.h"
#include "TimeSync.h"

// 前向声明
class BluetoothConfig;

// UART接收器类，处理单个串口的DMA接收，通过ID区分传感器
class UartReceiver {
public:
    UartReceiver();
    ~UartReceiver();
    
    // 初始化UART和DMA
    bool initialize(SensorData* sensorDataInstance = nullptr, TimeSync* timeSyncInstance = nullptr);
    
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
    
    // 设置蓝牙配置模块（用于转发配置信息）
    void setBluetoothConfig(BluetoothConfig* btConfig);
    
private:
    static const size_t RING_BUFFER_SIZE = 8192; // 增大缓冲区以处理多传感器数据
    static const size_t FRAME_SIZE = 43; // 帧头(1) + 时间戳(4) + 加速度(12) + 角速度(12) + 角度(12) + ID(1) + 帧尾(1)
    
    RingBuffer* ringBuffer; // 单个UART的环形缓冲区
    SensorData* sensorData;
    TimeSync* timeSync;
    BluetoothConfig* bluetoothConfig; // 蓝牙配置模块
    bool ownsSensorData; // 标记是否拥有SensorData实例
    SemaphoreHandle_t mutex;
    Stats stats;
    bool initialized;
    
    // 蓝牙数据包识别
    static const uint8_t BLE_DATA_HEADER[10]; // BLE DATA\r\n
    static const uint8_t BLE_DATA_FOOTER[16]; // +RECEIVED:1,43\r\n
    static const size_t BLE_PACKET_SIZE = 69; // 10 + 43 + 16
    static const size_t BLE_DATA_LENGTH = 43; // 传感器数据长度
    
    // 蓝牙数据包解析状态机
    enum class BlePacketState {
        IDLE,              // 空闲，寻找头部
        IN_HEADER,         // 正在匹配头部
        IN_DATA,           // 正在接收43字节数据
        IN_FOOTER,         // 正在匹配尾部
        COMPLETE           // 完整数据包接收完成
    };
    
    struct BlePacketParser {
        BlePacketState state;
        uint8_t headerMatchCount;   // 已匹配的头部字节数
        uint8_t dataBuffer[BLE_DATA_LENGTH]; // 43字节数据缓冲
        uint8_t dataCount;           // 已接收的数据字节数
        uint8_t footerMatchCount;    // 已匹配的尾部字节数
        uint8_t configBuffer[256];   // 配置数据缓冲（非蓝牙包数据）
        size_t configBufferPos;      // 配置缓冲区位置
    };
    
    BlePacketParser bleParser;
    
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
    
    // 蓝牙数据包状态机处理
    void processBleStateMachine(uint8_t byte);
    void resetBleParser();
    void handleCompleteBlePacket();
    void flushConfigBuffer();
    
    // DMA接收回调（占位符）
    static void dmaReceiveCallback(const uint8_t* data, size_t length);
};

#endif // UART_RECEIVER_H
