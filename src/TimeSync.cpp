#include "TimeSync.h"

TimeSync::TimeSync() {
    timeOffset = 0;
    lastSyncTime = 0;
    syncCount = 0;
    errorCount = 0;
    avgOffset = 0.0f;
    
    Serial.printf("[TimeSync] Created\n");
}

TimeSync::~TimeSync() {
    Serial.printf("[TimeSync] Destroyed\n");
}

bool TimeSync::initialize() {
    // 初始化时间同步系统
    lastSyncTime = millis();
    
    Serial.printf("[TimeSync] Initialized\n");
    return true;
}

uint32_t TimeSync::syncTimestamp(uint32_t sensorTimestamp, uint8_t sensorId) {
    uint32_t localTime = millis();
    
    // 计算时间偏移
    int32_t offset = calculateOffset(sensorTimestamp, localTime);
    
    // 更新平均偏移量
    updateAverageOffset(offset);
    
    // 应用偏移量
    uint32_t syncedTime = sensorTimestamp + timeOffset;
    
    syncCount++;
    lastSyncTime = localTime;
    
    return syncedTime;
}

uint32_t TimeSync::getLocalTimestamp() {
    return millis();
}

void TimeSync::setTimeOffset(int32_t offset) {
    timeOffset = offset;
    Serial.printf("[TimeSync] Time offset set to: %d ms\n", offset);
}

int32_t TimeSync::getTimeOffset() const {
    return timeOffset;
}

void TimeSync::reset() {
    timeOffset = 0;
    lastSyncTime = millis();
    syncCount = 0;
    errorCount = 0;
    avgOffset = 0.0f;
    
    Serial.printf("[TimeSync] Reset\n");
}

TimeSync::Stats TimeSync::getStats() const {
    Stats stats;
    stats.totalSyncs = syncCount;
    stats.syncErrors = errorCount;
    stats.currentOffset = timeOffset;
    stats.lastSyncTime = lastSyncTime;
    stats.avgOffset = avgOffset;
    return stats;
}

int32_t TimeSync::calculateOffset(uint32_t sensorTime, uint32_t localTime) {
    // 简单的时间偏移计算
    // 实际实现中可能需要更复杂的算法
    int32_t offset = (int32_t)localTime - (int32_t)sensorTime;
    
    // 限制偏移量范围
    if (offset > 10000) {
        offset = 10000;
        errorCount++;
    } else if (offset < -10000) {
        offset = -10000;
        errorCount++;
    }
    
    return offset;
}

void TimeSync::updateAverageOffset(int32_t newOffset) {
    if (syncCount == 0) {
        avgOffset = (float)newOffset;
    } else {
        // 使用指数移动平均
        float alpha = 0.1f;
        avgOffset = alpha * (float)newOffset + (1.0f - alpha) * avgOffset;
    }
    
    // 更新全局时间偏移
    timeOffset = (int32_t)avgOffset;
}
