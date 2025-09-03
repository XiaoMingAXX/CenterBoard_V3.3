#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <Arduino.h>
#include "SensorData.h"

// 缓冲池管理类
class BufferPool {
public:
    BufferPool();
    ~BufferPool();
    
    // 初始化缓冲池
    bool initialize(size_t poolSize = 20);
    
    // 获取数据块
    DataBlock* acquireBlock();
    
    // 释放数据块
    void releaseBlock(DataBlock* block);
    
    // 获取可用块数量
    size_t getAvailableBlocks() const;
    
    // 获取总块数量
    size_t getTotalBlocks() const;
    
    // 获取统计信息
    struct Stats {
        size_t totalBlocks;
        size_t availableBlocks;
        size_t usedBlocks;
        uint32_t totalAcquisitions;
        uint32_t totalReleases;
        uint32_t allocationFailures;
    };
    Stats getStats() const;
    
    // 重置统计信息
    void resetStats();
    
private:
    QueueHandle_t blockQueue;
    SemaphoreHandle_t mutex;
    Stats stats;
    size_t poolSize;
    
    // 预分配数据块
    void preallocateBlocks();
    
    // 创建新数据块
    DataBlock* createBlock();
};

#endif // BUFFER_POOL_H
