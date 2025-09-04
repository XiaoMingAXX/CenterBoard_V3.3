#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <Arduino.h>

// 前向声明
class BufferPool;

// 传感器数据类型定义
struct SensorFrame {
    uint8_t sensorId;        // 传感器ID (1-4)
    uint32_t timestamp;      // 原始时间戳
    uint32_t localTimestamp; // 本地接收时间戳
    float acc[3];           // 加速度 x,y,z
    float gyro[3];          // 角速度 x,y,z
    float angle[3];         // 角度 x,y,z
    bool valid;             // 数据有效性标志
};

// 批量数据块结构
struct DataBlock {
    static const size_t MAX_FRAMES = 30; // 每个块最大帧数
    SensorFrame frames[MAX_FRAMES];
    uint8_t frameCount;
    uint32_t blockId;
    uint32_t createTime;
    bool isFull;
};

// 传感器数据管理类
class SensorData {
public:
    SensorData(BufferPool* bufferPool = nullptr);
    ~SensorData();
    
    // 添加新的传感器帧
    bool addFrame(const SensorFrame& frame);
    
    // 获取下一个完整的数据块
    DataBlock* getNextBlock();
    
    // 释放数据块
    void releaseBlock(DataBlock* block);
    
    // 获取统计信息
    struct Stats {
        uint32_t totalFrames;
        uint32_t droppedFrames;
        uint32_t blocksCreated;
        uint32_t blocksSent;
        float avgFrameRate;
    };
    Stats getStats() const;
    
    // 重置统计信息
    void resetStats();
    
    // 获取传感器类型名称
    static const char* getSensorType(uint8_t sensorId);
    
private:
    DataBlock* currentBlock;
    QueueHandle_t blockQueue;
    SemaphoreHandle_t mutex;
    BufferPool* bufferPool;
    bool ownsBufferPool;
    Stats stats;
    uint32_t lastStatsTime;
    uint32_t frameCountSinceLastStats;
    
    void updateStats();
    DataBlock* createNewBlock();
};

#endif // SENSOR_DATA_H
