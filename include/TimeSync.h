#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
#include "Config.h"

// 滑动窗口大小
#define SLIDING_WINDOW_SIZE 50
// 传感器数量（与Config::SENSOR_COUNT保持一致）
#define TIME_SYNC_SENSOR_COUNT 4

// 时间同步管理类
class TimeSync {
public:
    TimeSync();
    ~TimeSync();
    
    // 初始化时间同步
    bool initialize();
    
    // 开始时间同步过程（在Core1上调用）
    bool startTimeSync();
    
    // 停止时间同步过程
    void stopTimeSync();
    
    // 开始后台拟合计算
    void startBackgroundFitting();
    
    // 停止后台拟合计算
    void stopBackgroundFitting();
    
    // 添加传感器时间戳对（S, E）到滑动窗口（快速操作）
    void addTimePair(uint8_t sensorId, uint32_t sensorTimeMs, int64_t espTimeUs);
    
    // 计算时间戳：T = a*S + b + N（快速操作）
    uint32_t calculateTimestamp(uint8_t sensorId, uint32_t sensorTimeMs);
    
    // 后台拟合计算（在后台任务中调用）
    void performBackgroundFitting();
    
    // 格式化时间戳为时/分/秒/毫秒格式
    uint32_t formatTimestamp(uint32_t timestampMs);
    
    // 获取NTP时间差N（毫秒）
    int64_t getNtpOffset() const;
    
    // 获取线性回归参数a和b（指定传感器）
    bool getLinearParams(uint8_t sensorId, float& a, float& b) const;
    
    // 检查时间同步是否就绪（指定传感器）
    bool isTimeSyncReady(uint8_t sensorId) const;
    
    // 检查所有传感器时间同步是否就绪
    bool isTimeSyncReady() const;
    
    // 检查时间同步是否激活
    bool isTimeSyncActive() const;
    
    // 重置时间同步
    void reset();
    
    // 重置所有传感器的计算状态（用于重新激活）
    void resetCalculationState();
    
    // 获取统计信息
    struct Stats {
        uint32_t totalPairs;
        uint32_t validPairs;
        int64_t ntpOffset;
        float linearParamA[TIME_SYNC_SENSOR_COUNT];
        float linearParamB[TIME_SYNC_SENSOR_COUNT];
        bool syncReady[TIME_SYNC_SENSOR_COUNT];
        uint32_t lastUpdateTime;
        uint32_t windowSize;
    };
    Stats getStats() const;
    
private:
    // 滑动窗口数据结构
    struct TimePair {
        uint8_t sensorId;       // 传感器ID (1-4)
        uint32_t sensorTimeMs;  // 传感器时间（毫秒）
        int64_t espTimeUs;      // ESP32时间（微秒）
        bool valid;             // 数据有效性
    };
    
    // 为每个传感器维护独立的滑动窗口
    TimePair slidingWindows[TIME_SYNC_SENSOR_COUNT][SLIDING_WINDOW_SIZE];
    uint8_t windowIndex[TIME_SYNC_SENSOR_COUNT];
    uint8_t windowCount[TIME_SYNC_SENSOR_COUNT];
    bool syncActive;
    bool fittingActive;
    bool syncReady[TIME_SYNC_SENSOR_COUNT];
    
    // NTP相关
    int64_t ntpOffsetMs;        // NTP时间差N（毫秒）
    bool ntpInitialized;
    
    // 为每个传感器维护独立的线性回归参数
    float paramA[TIME_SYNC_SENSOR_COUNT]; // 斜率参数a
    float paramB[TIME_SYNC_SENSOR_COUNT]; // 截距参数b
    bool paramsValid[TIME_SYNC_SENSOR_COUNT];
    
    // 每个传感器的计算次数和平均值
    uint8_t calcCount[TIME_SYNC_SENSOR_COUNT];           // 每个传感器已计算次数
    float paramASum[TIME_SYNC_SENSOR_COUNT];             // 参数A的累加和
    float paramBSum[TIME_SYNC_SENSOR_COUNT];             // 参数B的累加和
    bool calcCompleted[TIME_SYNC_SENSOR_COUNT];          // 每个传感器是否完成计算
    uint32_t lastCalcTime[TIME_SYNC_SENSOR_COUNT];       // 每个传感器上次计算时间
    
    // 互斥锁
    SemaphoreHandle_t mutex;
    
    // NTP时间同步
    bool syncNtpTime();
    static void ntpCallback(struct timeval* tv);
    static bool ntpCallbackCalled;
    
    // 最小二乘法计算线性回归参数（指定传感器）
    bool calculateLinearRegression(uint8_t sensorId, float& a, float& b);
    
    // 验证时间对的有效性
    bool isValidTimePair(uint8_t sensorId, uint32_t sensorTimeMs, int64_t espTimeUs);
    
    // 更新滑动窗口（指定传感器）
    void updateSlidingWindow(uint8_t sensorId, uint32_t sensorTimeMs, int64_t espTimeUs);
    
    // 验证传感器ID有效性
    bool isValidSensorId(uint8_t sensorId) const;
};

#endif // TIME_SYNC_H
