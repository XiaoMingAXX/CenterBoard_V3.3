#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>

// 时间同步管理类
class TimeSync {
public:
    TimeSync();
    ~TimeSync();
    
    // 初始化时间同步
    bool initialize();
    
    // 同步时间戳
    uint32_t syncTimestamp(uint32_t sensorTimestamp, uint8_t sensorId);
    
    // 获取本地时间戳
    uint32_t getLocalTimestamp();
    
    // 设置时间偏移
    void setTimeOffset(int32_t offset);
    
    // 获取时间偏移
    int32_t getTimeOffset() const;
    
    // 重置时间同步
    void reset();
    
    // 获取统计信息
    struct Stats {
        uint32_t totalSyncs;
        uint32_t syncErrors;
        int32_t currentOffset;
        uint32_t lastSyncTime;
        float avgOffset;
    };
    Stats getStats() const;
    
private:
    int32_t timeOffset;      // 时间偏移量（毫秒）
    uint32_t lastSyncTime;   // 上次同步时间
    uint32_t syncCount;      // 同步次数
    uint32_t errorCount;     // 错误次数
    float avgOffset;         // 平均偏移量
    
    // 计算时间偏移
    int32_t calculateOffset(uint32_t sensorTime, uint32_t localTime);
    
    // 更新平均偏移量
    void updateAverageOffset(int32_t newOffset);
};

#endif // TIME_SYNC_H
