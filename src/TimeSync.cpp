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
        sensorCalibrating[i] = false;
        autoCalibrationRounds[i] = 0;
    }
    
    syncActive = false;
    fittingActive = false;
    autoCalibrationActive = false;
    
    // 初始化NTP相关
    ntpOffsetMs = 0;
    ntpInitialized = false;
    
    // 创建互斥锁
    mutex = xSemaphoreCreateMutex();
    
    Serial0.printf("[TimeSync] Created with %d sensors\n", TIME_SYNC_SENSOR_COUNT);
}

TimeSync::~TimeSync() {
    stopTimeSync();
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    Serial0.printf("[TimeSync] Destroyed\n");
}

bool TimeSync::initialize() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // 初始化时间同步系统
        reset();
        xSemaphoreGive(mutex);
    }
    
    Serial0.printf("[TimeSync] Initialized\n");
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
        Serial0.printf("[TimeSync] NTP already initialized, using existing offset\n");
        return true;
    }
    
    if (syncNtpTime()) {
        Serial0.printf("[TimeSync] Started time synchronization\n");
        return true;
    } else {
        // NTP同步失败，重置状态
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            syncActive = false;
            xSemaphoreGive(mutex);
        }
        Serial0.printf("[TimeSync] ERROR: Failed to start NTP synchronization\n");
        return false;
    }
}

void TimeSync::stopTimeSync() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        syncActive = false;
        sntp_stop();
        xSemaphoreGive(mutex);
    }
    Serial0.printf("[TimeSync] Stopped time synchronization\n");
}

void TimeSync::startBackgroundFitting() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        fittingActive = true;
        xSemaphoreGive(mutex);
    }
    Serial0.printf("[TimeSync] Started background fitting\n");
    Serial0.printf("[TimeSync] Now collecting sensor time pairs for calibration...\n");
}

void TimeSync::stopBackgroundFitting() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        fittingActive = false;
        xSemaphoreGive(mutex);
    }
    Serial0.printf("[TimeSync] Stopped background fitting\n");
    Serial0.printf("[TimeSync] Stopped collecting sensor time pairs\n");
}

void TimeSync::startSingleSensorCalibration(uint8_t sensorId) {
    if (!isValidSensorId(sensorId)) {
        Serial0.printf("[TimeSync] ERROR: Invalid sensor ID: %d\n", sensorId);
        return;
    }
    
    uint8_t sensorIndex = sensorId - 1;
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // 清空该传感器的窗口
        windowIndex[sensorIndex] = 0;
        windowCount[sensorIndex] = 0;
        
        // 重置累加器（用于自动校准模式）
        paramASum[sensorIndex] = 0.0f;
        paramBSum[sensorIndex] = 0.0f;
        autoCalibrationRounds[sensorIndex] = 0;
        
        // 标记该传感器正在校准
        sensorCalibrating[sensorIndex] = true;
        
        xSemaphoreGive(mutex);
    }
    
    Serial0.printf("[TimeSync] Started single sensor calibration for sensor %d\n", sensorId);
}

void TimeSync::stopSingleSensorCalibration(uint8_t sensorId) {
    if (!isValidSensorId(sensorId)) {
        return;
    }
    
    uint8_t sensorIndex = sensorId - 1;
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        sensorCalibrating[sensorIndex] = false;
        xSemaphoreGive(mutex);
    }
    
    Serial0.printf("[TimeSync] Stopped single sensor calibration for sensor %d\n", sensorId);
}

void TimeSync::startAutoCalibration() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // 设置自动校准模式
        autoCalibrationActive = true;
        
        // 为所有传感器清空窗口和重置状态
        for (int i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            windowIndex[i] = 0;
            windowCount[i] = 0;
            paramASum[i] = 0.0f;
            paramBSum[i] = 0.0f;
            autoCalibrationRounds[i] = 0;
            sensorCalibrating[i] = true;  // 所有传感器都开始校准
        }
        
        xSemaphoreGive(mutex);
    }
    
    Serial0.printf("[TimeSync] ========================================\n");
    Serial0.printf("[TimeSync] Started auto-calibration (3 rounds per sensor)\n");
    Serial0.printf("[TimeSync] ========================================\n");
}

bool TimeSync::isSensorCalibrating(uint8_t sensorId) const {
    if (!isValidSensorId(sensorId) || xSemaphoreTake(mutex, 0) != pdTRUE) {
        return false;
    }
    
    uint8_t sensorIndex = sensorId - 1;
    bool calibrating = sensorCalibrating[sensorIndex];
    xSemaphoreGive(mutex);
    return calibrating;
}

void TimeSync::addTimePair(uint8_t sensorId, uint32_t sensorTimeMs, int64_t espTimeUs) {
    // 验证传感器ID有效性
    if (!isValidSensorId(sensorId)) {
        return;
    }
    
    uint8_t sensorIndex = sensorId - 1;
    
    // 检查是否需要收集该传感器的时间对
    // 1. 全局校准模式（fittingActive=true）
    // 2. 单个传感器校准模式（sensorCalibrating[sensorIndex]=true）
    if (!fittingActive && !sensorCalibrating[sensorIndex]) {
        return; // 该传感器未在校准中，直接返回
    }

    // 尝试获取互斥锁（快速获取，失败直接返回）
    if (xSemaphoreTake(mutex, 0) != pdTRUE) {
        return; // 无法获取锁，直接返回（避免阻塞数据接收）
    }
    
    // 验证时间对的有效性并快速添加到窗口
    if (isValidTimePair(sensorId, sensorTimeMs, espTimeUs)) {
        updateSlidingWindow(sensorId, sensorTimeMs, espTimeUs);
        // 注意：不在这里进行计算，在后台任务中处理
    }
    
    xSemaphoreGive(mutex);
}

uint64_t TimeSync::calculateTimestamp(uint8_t sensorId, uint32_t sensorTimeMs) {
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
    uint64_t globalTimeMs = (uint64_t)espTimeMs + ntpOffsetMs;
    
    xSemaphoreGive(mutex);
    return globalTimeMs;
}

uint32_t TimeSync::formatTimestamp(uint64_t timestampMs) {
    time_t timestamp = timestampMs / 1000;
    struct tm* timeinfo = localtime(&timestamp);
    if (!timeinfo) return 0;

    uint32_t ms = timestampMs % 1000;

    // 拼成十进制数：HHMMSSmmm
    uint32_t formatted = 
        timeinfo->tm_hour * 10000000 +
        timeinfo->tm_min  * 100000 +
        timeinfo->tm_sec  * 1000 +
        ms;

    return formatted;
}


void TimeSync::performBackgroundFitting() {
    // 在后台任务中检查并处理校准计算
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // 无法获取锁，下次再处理
    }
    
    // 检查每个传感器的窗口状态
    for (uint8_t sensorIndex = 0; sensorIndex < TIME_SYNC_SENSOR_COUNT; sensorIndex++) {
        uint8_t sensorId = sensorIndex + 1;
        
        // 检查该传感器是否正在校准
        if (!fittingActive && !sensorCalibrating[sensorIndex]) {
            continue; // 该传感器未在校准中，跳过
        }
        
        // 检查窗口是否已满（50个点）
        if (windowCount[sensorIndex] >= SLIDING_WINDOW_SIZE) {
            // 窗口已满，进行计算
            float tempA, tempB;
            if (calculateLinearRegression(sensorId, tempA, tempB)) {
                // 自动校准模式：累加参数并计数
                if (autoCalibrationActive && sensorCalibrating[sensorIndex]) {
                    paramASum[sensorIndex] += tempA;
                    paramBSum[sensorIndex] += tempB;
                    autoCalibrationRounds[sensorIndex]++;
                    
                    Serial0.printf("[TimeSync] Sensor %d auto-calibration round %d: a=%.6f, b=%.2f\n", 
                                 sensorId, autoCalibrationRounds[sensorIndex], tempA, tempB);
                    
                    // 检查是否完成3轮
                    if (autoCalibrationRounds[sensorIndex] >= 3) {
                        // 计算平均值
                        paramA[sensorIndex] = paramASum[sensorIndex] / 3.0f;
                        paramB[sensorIndex] = paramBSum[sensorIndex] / 3.0f;
                        paramsValid[sensorIndex] = true;
                        syncReady[sensorIndex] = true;
                        sensorCalibrating[sensorIndex] = false;
                        
                        Serial0.printf("[TimeSync] Sensor %d auto-calibration completed: avg_a=%.6f, avg_b=%.2f\n", 
                                     sensorId, paramA[sensorIndex], paramB[sensorIndex]);
                    }
                }
                // 单次校准模式：直接使用参数
                else if (sensorCalibrating[sensorIndex]) {
                    paramA[sensorIndex] = tempA;
                    paramB[sensorIndex] = tempB;
                    paramsValid[sensorIndex] = true;
                    syncReady[sensorIndex] = true;
                    sensorCalibrating[sensorIndex] = false;
                    
                    Serial0.printf("[TimeSync] Sensor %d single calibration completed: a=%.6f, b=%.2f\n", 
                                 sensorId, paramA[sensorIndex], paramB[sensorIndex]);
                }
                
                // 清空窗口，准备下次校准
                windowIndex[sensorIndex] = 0;
                windowCount[sensorIndex] = 0;
            }
        }
    }
    
    // 检查自动校准是否全部完成
    if (autoCalibrationActive) {
        bool allCompleted = true;
        for (uint8_t i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
            if (autoCalibrationRounds[i] < 3) {
                allCompleted = false;
                break;
            }
        }
        
        if (allCompleted) {
            // 所有传感器都完成了自动校准
            autoCalibrationActive = false;
            Serial0.printf("[TimeSync] ========================================\n");
            Serial0.printf("[TimeSync] Auto-calibration completed for all sensors!\n");
            for (uint8_t i = 0; i < TIME_SYNC_SENSOR_COUNT; i++) {
                Serial0.printf("[TimeSync] Sensor %d: a=%.6f, b=%.2f\n", 
                             i + 1, paramA[i], paramB[i]);
            }
            Serial0.printf("[TimeSync] ========================================\n");
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

bool TimeSync::isNtpInitialized() const {
    // ntpInitialized 不需要mutex保护，因为它只在初始化时设置一次
    return ntpInitialized;
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
    
    Serial0.printf("[TimeSync] Reset all sensors\n");
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
        Serial0.printf("[TimeSync] Reset calculation state for all sensors\n");
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
        Serial0.printf("[TimeSync] NTP already initialized\n");
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

        time_t now = time(NULL);
        struct tm *timeinfo = localtime(&now);
        Serial0.printf("[TimeSync] localtime: %02d:%02d:%02d\n",
                    timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

        Serial0.printf("[TimeSync] NTP synchronized, offset: %lld ms\n", ntpOffsetMs);
        Serial0.printf("[TimeSync] NTP time: %lld ms, System uptime: %lld ms\n", ntpTimeMs, systemUptimeMs);
        return true;
    } else {
        Serial0.printf("[TimeSync] ERROR: NTP synchronization timeout\n");
        return false;
    }
}

void TimeSync::ntpCallback(struct timeval* tv) {
    ntpCallbackCalled = true;
    Serial0.printf("[TimeSync] NTP callback called\n");
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
    
    Serial0.printf("[TimeSync] Sensor %d linear regression: a=%.6f, b=%.2f (valid pairs: %d)\n", 
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
        Serial0.printf("[TimeSync] DEBUG: Sensor %d time pair %d - sensor: %u ms, esp: %lld us\n", 
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
