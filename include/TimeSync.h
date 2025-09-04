#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>

// 滑动窗口大小
#define SLIDING_WINDOW_SIZE 50

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
    void addTimePair(uint32_t sensorTimeMs, int64_t espTimeUs);
    
    // 计算时间戳：T = a*S + b + N（快速操作）
    uint32_t calculateTimestamp(uint32_t sensorTimeMs);
    
    // 后台拟合计算（在后台任务中调用）
    void performBackgroundFitting();
    
    // 格式化时间戳为时/分/秒/毫秒格式
    uint32_t formatTimestamp(uint32_t timestampMs);
    
    // 获取NTP时间差N（毫秒）
    int64_t getNtpOffset() const;
    
    // 获取线性回归参数a和b
    bool getLinearParams(float& a, float& b) const;
    
    // 检查时间同步是否就绪
    bool isTimeSyncReady() const;
    
    // 重置时间同步
    void reset();
    
    // 获取统计信息
    struct Stats {
        uint32_t totalPairs;
        uint32_t validPairs;
        int64_t ntpOffset;
        float linearParamA;
        float linearParamB;
        bool syncReady;
        uint32_t lastUpdateTime;
        uint32_t windowSize;
    };
    Stats getStats() const;
    
private:
    // 滑动窗口数据结构
    struct TimePair {
        uint32_t sensorTimeMs;  // 传感器时间（毫秒）
        int64_t espTimeUs;      // ESP32时间（微秒）
        bool valid;             // 数据有效性
    };
    
    TimePair slidingWindow[SLIDING_WINDOW_SIZE];
    uint8_t windowIndex;
    uint8_t windowCount;
    bool syncActive;
    bool fittingActive;
    bool syncReady;
    
    // NTP相关
    int64_t ntpOffsetMs;        // NTP时间差N（毫秒）
    bool ntpInitialized;
    
    // 线性回归参数
    float paramA;               // 斜率参数a
    float paramB;               // 截距参数b
    bool paramsValid;
    
    // 互斥锁
    SemaphoreHandle_t mutex;
    
    // NTP时间同步
    bool syncNtpTime();
    static void ntpCallback(struct timeval* tv);
    static bool ntpCallbackCalled;
    
    // 最小二乘法计算线性回归参数
    bool calculateLinearRegression(float& a, float& b);
    
    // 验证时间对的有效性
    bool isValidTimePair(uint32_t sensorTimeMs, int64_t espTimeUs);
    
    // 更新滑动窗口
    void updateSlidingWindow(uint32_t sensorTimeMs, int64_t espTimeUs);
};

#endif // TIME_SYNC_H
