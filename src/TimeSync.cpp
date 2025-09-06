#include "TimeSync.h"

// 静态成员初始化
bool TimeSync::ntpCallbackCalled = false;

TimeSync::TimeSync() {
    // 初始化所有传感器的滑动窗口
    memset(slidingWindows, 0, sizeof(slidingWindows));
    for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
        windowIndex[i] = 0;
        windowCount[i] = 0;
        syncReady[i] = false;
        paramA[i] = 1.0f;
        paramB[i] = 0.0f;
        paramsValid[i] = false;
        calcCount[i] = 0;
        paramASum[i] = 0.0f;
        paramBSum[i] = 0.0f;
        calcCompleted[i] = false;
        lastCalcTime[i] = 0;
    }
    
    syncActive = false;
    fittingActive = false;
    
    // 初始化NTP相关
    ntpOffsetMs = 0;
    ntpInitialized = false;
    
    // 创建互斥锁
    mutex = xSemaphoreCreateMutex();
    
    Serial.printf("[TimeSync] Created with %d sensors\n", TIME_SYNC_SENSOR_COUNT);
}

TimeSync::~TimeSync() {
    stopTimeSync();
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    Serial.printf("[TimeSync] Destroyed\n");
}

bool TimeSync::initialize() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // 初始化时间同步系统
        reset();
        xSemaphoreGive(mutex);
    }
    
    Serial.printf("[TimeSync] Initialized\n");
    return true;
}

bool TimeSync::startTimeSync() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        if (syncActive) {
            xSemaphoreGive(mutex);
            return true; // 已经在同步中
        }
        
        // 重置状态
        reset();
        syncActive = true;
        xSemaphoreGive(mutex); // 释放锁，允许addTimePair正常工作
    } else {
        return false;
    }
    
    // 在锁外进行NTP时间同步（这可能需要几秒钟）
    // 如果NTP已经初始化，直接返回成功
    if (ntpInitialized) {
        Serial.printf("[TimeSync] NTP already initialized, using existing offset\n");
        return true;
    }
    
    if (syncNtpTime()) {
        Serial.printf("[TimeSync] Started time synchronization\n");
        return true;
    } else {
        // NTP同步失败，重置状态
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            syncActive = false;
            xSemaphoreGive(mutex);
        }
        Serial.printf("[TimeSync] ERROR: Failed to start NTP synchronization\n");
        return false;
    }
}

void TimeSync::stopTimeSync() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        syncActive = false;
        sntp_stop();
        xSemaphoreGive(mutex);
    }
    Serial.printf("[TimeSync] Stopped time synchronization\n");
}

void TimeSync::startBackgroundFitting() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        fittingActive = true;
        xSemaphoreGive(mutex);
    }
    Serial.printf("[TimeSync] Started background fitting\n");
}

void TimeSync::stopBackgroundFitting() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        fittingActive = false;
        xSemaphoreGive(mutex);
    }
    Serial.printf("[TimeSync] Stopped background fitting\n");
}

void TimeSync::addTimePair(uint8_t sensorId, uint32_t sensorTimeMs, int64_t espTimeUs) {
    // 首先检查同步是否激活
    if (!syncActive) {
        return; // 同步未激活，直接返回
    }

    // 尝试获取互斥锁
    if (xSemaphoreTake(mutex, 0) != pdTRUE) {
        Serial.printf("[TimeSync] WARNING: Failed to acquire mutex in addTimePair\n");
        return; // 无法获取锁，直接返回
    }
    
    // 验证传感器ID有效性
    if (!isValidSensorId(sensorId)) {
        Serial.printf("[TimeSync] WARNING: Invalid sensor ID: %d\n", sensorId);
        xSemaphoreGive(mutex);
        return;
    }
    
    // 验证时间对的有效性
    if (isValidTimePair(sensorId, sensorTimeMs, espTimeUs)) {
        updateSlidingWindow(sensorId, sensorTimeMs, espTimeUs);
        // 注意：不在这里进行拟合计算，避免影响数据接收速度
    } else {
        Serial.printf("[TimeSync] WARNING: Invalid time pair: sensorId=%d, sensorTime=%u, espTime=%lld\n", 
                     sensorId, sensorTimeMs, espTimeUs);
    }
    
    xSemaphoreGive(mutex);
}

uint32_t TimeSync::calculateTimestamp(uint8_t sensorId, uint32_t sensorTimeMs) {
    if (!isValidSensorId(sensorId) || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return sensorTimeMs; // 返回原始时间戳
    }
    
    uint8_t sensorIndex = sensorId - 1; // 转换为数组索引 (1-4 -> 0-3)
    
    // 如果该传感器的时间同步未就绪，返回原始时间戳
    if (!syncReady[sensorIndex]) {
        xSemaphoreGive(mutex);
        return sensorTimeMs;
    }
    
    // 计算：T = a*S + b + N
    // 注意：现在paramB已经是毫秒单位了
    float espTimeMs = paramA[sensorIndex] * sensorTimeMs + paramB[sensorIndex];
    uint32_t globalTimeMs = (uint32_t)espTimeMs + ntpOffsetMs;
    
    xSemaphoreGive(mutex);
    return globalTimeMs;
}

uint32_t TimeSync::formatTimestamp(uint32_t timestampMs) {
    // 格式化为时/分/秒/毫秒格式 (HHMMSSmmm)
    time_t timestamp = timestampMs / 1000;
    struct tm* timeinfo = localtime(&timestamp);
    
    if (!timeinfo) {
        return 0;
    }
    
    uint32_t formatted = 0;
    formatted |= (timeinfo->tm_hour % 24) << 20;  // 时 (5位)
    formatted |= (timeinfo->tm_min % 60) << 14;   // 分 (6位)
    formatted |= (timeinfo->tm_sec % 60) << 7;    // 秒 (7位)
    formatted |= (timestampMs % 1000) & 0x7F;     // 毫秒 (7位)
    
    return formatted;
}

void TimeSync::performBackgroundFitting() {
    if (!fittingActive || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return;
    }
    
    uint32_t currentTime = millis();
    
    // 为每个传感器进行独立的拟合计算
    for (uint8_t sensorIndex = 0; sensorIndex < TIME_SYNC_SENSOR_COUNT; sensorIndex++) {
        uint8_t sensorId = sensorIndex + 1; // 转换为传感器ID (1-4)
        
        // 如果该传感器已完成计算，跳过
        if (calcCompleted[sensorIndex]) {
            continue;
        }
        
        // 检查是否到了计算时间
        if (currentTime - lastCalcTime[sensorIndex] < Config::TIME_SYNC_CALC_INTERVAL_MS) {
            continue;
        }
        
        // 当窗口有足够数据时，计算线性回归参数
        if (windowCount[sensorIndex] >= 10) { // 至少需要10个数据点
            float tempA, tempB;
            if (calculateLinearRegression(sensorId, tempA, tempB)) {
                // 累加参数值
                paramASum[sensorIndex] += tempA;
                paramBSum[sensorIndex] += tempB;
                calcCount[sensorIndex]++;
                lastCalcTime[sensorIndex] = currentTime;
                
                Serial.printf("[TimeSync] Sensor %d calculation %d/%d: a=%.6f, b=%.2f\n", 
                             sensorId, calcCount[sensorIndex], Config::TIME_SYNC_CALC_COUNT, tempA, tempB);
                
                // 检查是否完成指定次数的计算
                if (calcCount[sensorIndex] >= Config::TIME_SYNC_CALC_COUNT) {
                    // 计算平均值
                    paramA[sensorIndex] = paramASum[sensorIndex] / calcCount[sensorIndex];
                    paramB[sensorIndex] = paramBSum[sensorIndex] / calcCount[sensorIndex];
                    paramsValid[sensorIndex] = true;
                    syncReady[sensorIndex] = true;
                    calcCompleted[sensorIndex] = true;
                    
                    Serial.printf("[TimeSync] Sensor %d completed: avg_a=%.6f, avg_b=%.2f\n", 
                                 sensorId, paramA[sensorIndex], paramB[sensorIndex]);
                }
            }
        }
    }
    
    xSemaphoreGive(mutex);
}

int64_t TimeSync::getNtpOffset() const {
    return ntpOffsetMs;
}

bool TimeSync::getLinearParams(uint8_t sensorId, float& a, float& b) const {
    if (!isValidSensorId(sensorId) || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return false;
    }
    
    uint8_t sensorIndex = sensorId - 1; // 转换为数组索引 (1-4 -> 0-3)
    
    if (paramsValid[sensorIndex]) {
        a = paramA[sensorIndex];
        b = paramB[sensorIndex];
        xSemaphoreGive(mutex);
        return true;
    }
    
    xSemaphoreGive(mutex);
    return false;
}

bool TimeSync::isTimeSyncReady(uint8_t sensorId) const {
    if (!isValidSensorId(sensorId) || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return false;
    }
    
    uint8_t sensorIndex = sensorId - 1; // 转换为数组索引 (1-4 -> 0-3)
    bool ready = syncReady[sensorIndex];
    xSemaphoreGive(mutex);
    return ready;
}

bool TimeSync::isTimeSyncReady() const {
    if (xSemaphoreTake(mutex, 0) == pdTRUE) {
        // 检查所有传感器是否都已就绪
        bool allReady = true;
        for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            if (!syncReady[i]) {
                allReady = false;
                break;
            }
        }
        xSemaphoreGive(mutex);
        return allReady;
    }
    return false;
}

bool TimeSync::isTimeSyncActive() const {
    if (xSemaphoreTake(mutex, 0) == pdTRUE) {
        bool active = syncActive;
        xSemaphoreGive(mutex);
        return active;
    }
    return false;
}

void TimeSync::reset() {
    // 重置所有传感器的滑动窗口
    memset(slidingWindows, 0, sizeof(slidingWindows));
    for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
        windowIndex[i] = 0;
        windowCount[i] = 0;
        paramA[i] = 1.0f;
        paramB[i] = 0.0f;
        paramsValid[i] = false;
        syncReady[i] = false;
        calcCount[i] = 0;
        paramASum[i] = 0.0f;
        paramBSum[i] = 0.0f;
        calcCompleted[i] = false;
        lastCalcTime[i] = 0;
    }
    
    fittingActive = false;
    
    Serial.printf("[TimeSync] Reset all sensors\n");
}

void TimeSync::resetCalculationState() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // 重置所有传感器的计算状态，但保留滑动窗口数据
        for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            calcCount[i] = 0;
            paramASum[i] = 0.0f;
            paramBSum[i] = 0.0f;
            calcCompleted[i] = false;
            lastCalcTime[i] = 0;
            // 不重置syncReady和paramsValid，保持已完成的传感器状态
        }
        xSemaphoreGive(mutex);
        Serial.printf("[TimeSync] Reset calculation state for all sensors\n");
    }
}

TimeSync::Stats TimeSync::getStats() const {
    Stats stats;
    if (xSemaphoreTake(mutex, 0) == pdTRUE) {
        // 计算总的数据对数量
        stats.totalPairs = 0;
        stats.validPairs = 0;
        for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            stats.totalPairs += windowCount[i];
            stats.validPairs += windowCount[i]; // 简化处理，假设所有数据都有效
        }
        
        stats.ntpOffset = ntpOffsetMs;
        
        // 复制所有传感器的参数
        for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            stats.linearParamA[i] = paramA[i];
            stats.linearParamB[i] = paramB[i];
            stats.syncReady[i] = syncReady[i];
        }
        
        stats.lastUpdateTime = millis();
        stats.windowSize = SLIDING_WINDOW_SIZE;
        xSemaphoreGive(mutex);
    }
    return stats;
}

bool TimeSync::syncNtpTime() {
    // 如果NTP已经初始化，直接返回成功
    if (ntpInitialized) {
        Serial.printf("[TimeSync] NTP already initialized\n");
        return true;
    }
    
    // 配置NTP服务器（使用中国NTP服务器）
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_setservername(1, "ntp1.aliyun.com");
    sntp_setservername(2, "time.windows.com");
    
    // 设置时区为北京时间 (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();
    
    // 设置NTP回调
    sntp_set_time_sync_notification_cb(ntpCallback);
    
    // 启动SNTP
    sntp_init();
    
    // 等待NTP同步完成
    uint32_t timeout = 0;
    while (!ntpCallbackCalled && timeout < 10000) { // 10秒超时
        delay(100);
        timeout += 100;
    }
    
    if (ntpCallbackCalled) {
        // 计算NTP时间差
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t ntpTimeMs = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        
        // 获取系统启动时间（毫秒）
        int64_t systemUptimeMs = millis();
        
        // 计算NTP时间与系统启动时间的差值
        // 这个差值表示系统启动时对应的NTP时间
        ntpOffsetMs = ntpTimeMs - systemUptimeMs;
        ntpInitialized = true;
        
        Serial.printf("[TimeSync] NTP synchronized, offset: %lld ms\n", ntpOffsetMs);
        Serial.printf("[TimeSync] NTP time: %lld ms, System uptime: %lld ms\n", ntpTimeMs, systemUptimeMs);
        return true;
    } else {
        Serial.printf("[TimeSync] ERROR: NTP synchronization timeout\n");
        return false;
    }
}

void TimeSync::ntpCallback(struct timeval* tv) {
    ntpCallbackCalled = true;
    Serial.printf("[TimeSync] NTP callback called\n");
    // 注意：这里不能直接设置ntpInitialized，因为这是静态回调函数
    // ntpInitialized会在syncNtpTime中设置
}

bool TimeSync::calculateLinearRegression(uint8_t sensorId, float& a, float& b) {
    if (!isValidSensorId(sensorId)) {
        return false;
    }
    
    uint8_t sensorIndex = sensorId - 1; // 转换为数组索引 (1-4 -> 0-3)
    
    if (windowCount[sensorIndex] < 2) {
        return false;
    }
    
    // 最小二乘法计算线性回归参数
    // y = ax + b，其中 x = sensorTimeMs, y = espTimeUs
    // 注意：传感器时间是毫秒，ESP32时间是微秒，需要统一单位
    float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
    uint8_t validCount = 0;
    
    for (uint8_t i = 0; i < windowCount[sensorIndex]; i++) {
        if (slidingWindows[sensorIndex][i].valid) {
            float x = (float)slidingWindows[sensorIndex][i].sensorTimeMs;  // 传感器时间(ms)
            float y = (float)slidingWindows[sensorIndex][i].espTimeUs / 1000.0f;  // ESP32时间转换为ms
            
            sumX += x;
            sumY += y;
            sumXY += x * y;
            sumXX += x * x;
            validCount++;
        }
    }
    
    if (validCount < 2) {
        return false;
    }
    
    // 计算斜率 a 和截距 b
    float n = (float)validCount;
    float denominator = n * sumXX - sumX * sumX;
    
    if (abs(denominator) < 1e-6) {
        return false; // 避免除零
    }
    
    // a = (n * sumXY - sumX * sumY) / denominator;
    a = 1;
    b = (sumY - a * sumX) / n;
    
    Serial.printf("[TimeSync] Sensor %d linear regression: a=%.6f, b=%.2f (valid pairs: %d)\n", 
                  sensorId, a, b, validCount);
    
    return true;
}

bool TimeSync::isValidTimePair(uint8_t sensorId, uint32_t sensorTimeMs, int64_t espTimeUs) {
    // 验证传感器ID
    if (!isValidSensorId(sensorId)) {
        return false;
    }
    
    // 基本有效性检查
    if (sensorTimeMs == 0 || espTimeUs <= 0) {
        return false;
    }
    
    // 检查传感器时间戳是否在合理范围内（传感器可能运行了很长时间）
    // 传感器时间戳通常从0开始递增，检查是否在合理范围内
    if (sensorTimeMs > 0xFFFFFFFF - 1000) { // 避免溢出
        return false;
    }
    
    // 检查ESP32时间是否合理（微秒）
    if (espTimeUs > 0x7FFFFFFFFFFFFFFF) { // 避免溢出
        return false;
    }
    
    return true;
}

void TimeSync::updateSlidingWindow(uint8_t sensorId, uint32_t sensorTimeMs, int64_t espTimeUs) {
    uint8_t sensorIndex = sensorId - 1; // 转换为数组索引 (1-4 -> 0-3)
    
    // 添加到对应传感器的滑动窗口
    slidingWindows[sensorIndex][windowIndex[sensorIndex]].sensorId = sensorId;
    slidingWindows[sensorIndex][windowIndex[sensorIndex]].sensorTimeMs = sensorTimeMs;
    slidingWindows[sensorIndex][windowIndex[sensorIndex]].espTimeUs = espTimeUs;
    slidingWindows[sensorIndex][windowIndex[sensorIndex]].valid = true;
    
    // 调试信息：显示前几个时间对
    if (windowCount[sensorIndex] < 5) {
        Serial.printf("[TimeSync] DEBUG: Sensor %d time pair %d - sensor: %u ms, esp: %lld us\n", 
                     sensorId, windowCount[sensorIndex], sensorTimeMs, espTimeUs);
    }
    
    // 更新索引和计数
    windowIndex[sensorIndex] = (windowIndex[sensorIndex] + 1) % SLIDING_WINDOW_SIZE;
    if (windowCount[sensorIndex] < SLIDING_WINDOW_SIZE) {
        windowCount[sensorIndex]++;
    }
}

bool TimeSync::isValidSensorId(uint8_t sensorId) const {
    return (sensorId >= 1 && sensorId <= TIME_SYNC_SENSOR_COUNT);
}
