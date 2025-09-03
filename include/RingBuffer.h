#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <Arduino.h>

// 环形缓冲区类，用于UART DMA接收
class RingBuffer {
public:
    RingBuffer(size_t size);
    ~RingBuffer();
    
    // 写入数据
    bool write(const uint8_t* data, size_t length);
    
    // 读取数据
    size_t read(uint8_t* buffer, size_t maxLength);
    
    // 获取可读数据长度
    size_t available() const;
    
    // 获取可写空间
    size_t freeSpace() const;
    
    // 清空缓冲区
    void clear();
    
    // 获取缓冲区统计信息
    struct Stats {
        size_t totalWrites;
        size_t totalReads;
        size_t overflows;
        size_t underflows;
    };
    Stats getStats() const;
    
private:
    uint8_t* buffer;
    size_t size;
    volatile size_t writePos;
    volatile size_t readPos;
    volatile size_t dataLength;
    mutable SemaphoreHandle_t mutex;
    Stats stats;
    
    size_t getWriteSpace() const;
    size_t getReadData() const;
};

#endif // RING_BUFFER_H
