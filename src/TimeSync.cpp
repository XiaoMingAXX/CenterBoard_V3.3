#include "TimeSync.h"

// 静态成员初始化
bool TimeSync::ntpCallbackCalled = false;

TimeSync::TimeSync() {
    // 初始化滑动窗口
    memset(slidingWindow, 0, sizeof(slidingWindow));
    windowIndex = 0;
    windowCount = 0;
    syncActive = false;
    fittingActive = false;
    syncReady = false;
    
    // 初始化NTP相关
    ntpOffsetMs = 0;
    ntpInitialized = false;
    
    // 初始化线性回归参数
    paramA = 1.0f;
    paramB = 0.0f;
    paramsValid = false;
    
    // 创建互斥锁
    mutex = xSemaphoreCreateMutex();
    
    Serial.printf("[TimeSync] Created\n");
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
        
        // 开始NTP时间同步
        if (syncNtpTime()) {
            Serial.printf("[TimeSync] Started time synchronization\n");
            xSemaphoreGive(mutex);
            return true;
        } else {
            syncActive = false;
            Serial.printf("[TimeSync] ERROR: Failed to start NTP synchronization\n");
            xSemaphoreGive(mutex);
            return false;
        }
    }
    return false;
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

void TimeSync::addTimePair(uint32_t sensorTimeMs, int64_t espTimeUs) {
    if (!syncActive || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return; // 同步未激活或无法获取锁
    }
    
    // 验证时间对的有效性
    if (isValidTimePair(sensorTimeMs, espTimeUs)) {
        updateSlidingWindow(sensorTimeMs, espTimeUs);
        // 注意：不在这里进行拟合计算，避免影响数据接收速度
    }
    
    xSemaphoreGive(mutex);
}

uint32_t TimeSync::calculateTimestamp(uint32_t sensorTimeMs) {
    if (!syncReady || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return sensorTimeMs; // 返回原始时间戳
    }
    
    // 计算：T = a*S + b + N
    // 注意：这里需要将微秒转换为毫秒
    float espTimeMs = paramA * sensorTimeMs + paramB / 1000.0f; // paramB是微秒，转换为毫秒
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
    
    // 当窗口有足够数据时，计算线性回归参数
    if (windowCount >= 10) { // 至少需要10个数据点
        float tempA, tempB;
        if (calculateLinearRegression(tempA, tempB)) {
            paramA = tempA;
            paramB = tempB;
            paramsValid = true;
            syncReady = true;
            
            Serial.printf("[TimeSync] Background fitting completed: a=%.6f, b=%.2f\n", paramA, paramB);
        }
    }
    
    xSemaphoreGive(mutex);
}

int64_t TimeSync::getNtpOffset() const {
    return ntpOffsetMs;
}

bool TimeSync::getLinearParams(float& a, float& b) const {
    if (xSemaphoreTake(mutex, 0) == pdTRUE) {
        if (paramsValid) {
            a = paramA;
            b = paramB;
            xSemaphoreGive(mutex);
            return true;
        }
        xSemaphoreGive(mutex);
    }
    return false;
}

bool TimeSync::isTimeSyncReady() const {
    if (xSemaphoreTake(mutex, 0) == pdTRUE) {
        bool ready = syncReady;
        xSemaphoreGive(mutex);
        return ready;
    }
    return false;
}

void TimeSync::reset() {
    // 重置滑动窗口
    memset(slidingWindow, 0, sizeof(slidingWindow));
    windowIndex = 0;
    windowCount = 0;
    
    // 重置参数
    paramA = 1.0f;
    paramB = 0.0f;
    paramsValid = false;
    syncReady = false;
    fittingActive = false;
    
    Serial.printf("[TimeSync] Reset\n");
}

TimeSync::Stats TimeSync::getStats() const {
    Stats stats;
    if (xSemaphoreTake(mutex, 0) == pdTRUE) {
        stats.totalPairs = windowCount;
        stats.validPairs = windowCount; // 简化处理，假设所有数据都有效
        stats.ntpOffset = ntpOffsetMs;
        stats.linearParamA = paramA;
        stats.linearParamB = paramB;
        stats.syncReady = syncReady;
        stats.lastUpdateTime = millis();
        stats.windowSize = SLIDING_WINDOW_SIZE;
        xSemaphoreGive(mutex);
    }
    return stats;
}

bool TimeSync::syncNtpTime() {
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
        int64_t espTimeMs = millis(); // 使用millis()而不是esp_timer_get_time()
        ntpOffsetMs = ntpTimeMs - espTimeMs;
        ntpInitialized = true;
        
        Serial.printf("[TimeSync] NTP synchronized, offset: %lld ms\n", ntpOffsetMs);
        return true;
    } else {
        Serial.printf("[TimeSync] ERROR: NTP synchronization timeout\n");
        return false;
    }
}

void TimeSync::ntpCallback(struct timeval* tv) {
    ntpCallbackCalled = true;
    Serial.printf("[TimeSync] NTP callback called\n");
}

bool TimeSync::calculateLinearRegression(float& a, float& b) {
    if (windowCount < 2) {
        return false;
    }
    
    // 最小二乘法计算线性回归参数
    // y = ax + b，其中 x = sensorTimeMs, y = espTimeUs
    float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
    uint8_t validCount = 0;
    
    for (uint8_t i = 0; i < windowCount; i++) {
        if (slidingWindow[i].valid) {
            float x = (float)slidingWindow[i].sensorTimeMs;
            float y = (float)slidingWindow[i].espTimeUs;
            
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
    
    a = (n * sumXY - sumX * sumY) / denominator;
    b = (sumY - a * sumX) / n;
    
    paramsValid = true;
    
    Serial.printf("[TimeSync] Linear regression: a=%.6f, b=%.2f (valid pairs: %d)\n", 
                  a, b, validCount);
    
    return true;
}

bool TimeSync::isValidTimePair(uint32_t sensorTimeMs, int64_t espTimeUs) {
    // 基本有效性检查
    if (sensorTimeMs == 0 || espTimeUs <= 0) {
        return false;
    }
    
    // 检查时间是否合理（传感器时间应该在合理范围内）
    uint32_t currentMs = millis();
    if (sensorTimeMs > currentMs + 1000 || sensorTimeMs < currentMs - 86400000) { // 1秒未来，1天过去
        return false;
    }
    
    return true;
}

void TimeSync::updateSlidingWindow(uint32_t sensorTimeMs, int64_t espTimeUs) {
    // 添加到滑动窗口
    slidingWindow[windowIndex].sensorTimeMs = sensorTimeMs;
    slidingWindow[windowIndex].espTimeUs = espTimeUs;
    slidingWindow[windowIndex].valid = true;
    
    // 更新索引和计数
    windowIndex = (windowIndex + 1) % SLIDING_WINDOW_SIZE;
    if (windowCount < SLIDING_WINDOW_SIZE) {
        windowCount++;
    }
}
