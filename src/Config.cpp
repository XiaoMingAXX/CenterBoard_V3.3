#include "Config.h"

// 网络配置
const char* Config::WIFI_SSID = "xiaoming";
const char* Config::WIFI_PASSWORD = "LZMSDSG0704";
const char* Config::SERVER_URL = "175.178.100.179";
const uint16_t Config::SERVER_PORT = 8000;
const char* Config::WEBSOCKET_PATH = "/ws/esp32/";  // 基础路径，device_code会动态添加

// UART配置
const uint32_t Config::UART_BAUD_RATE = 460800;
const int Config::UART_TX_PIN = 17;
const int Config::UART_RX_PIN = 16;

// 缓冲区配置
const size_t Config::RING_BUFFER_SIZE = 4096;
const size_t Config::BLOCK_POOL_SIZE = 20;
const size_t Config::MAX_FRAMES_PER_BLOCK = 30;

// 任务配置
const uint32_t Config::UART_TASK_STACK_SIZE = 4096;
const uint32_t Config::NETWORK_TASK_STACK_SIZE = 16384;  // 从8192增加到16384
const uint32_t Config::CLI_TASK_STACK_SIZE = 2048;
const uint32_t Config::MONITOR_TASK_STACK_SIZE = 2048;

const uint32_t Config::UART_TASK_PRIORITY = 3;
const uint32_t Config::NETWORK_TASK_PRIORITY = 2;
const uint32_t Config::CLI_TASK_PRIORITY = 1;
const uint32_t Config::MONITOR_TASK_PRIORITY = 1;

// 传感器配置
const uint8_t Config::SENSOR_COUNT = 4;
const uint8_t Config::FRAME_SIZE = 43; // 帧头(1) + 时间戳(4) + 加速度(12) + 角速度(12) + 角度(12) + ID(1) + 帧尾(1)

// 时间配置
const uint32_t Config::HEARTBEAT_INTERVAL = 30000;    // 30秒
const uint32_t Config::STATUS_INTERVAL = 30000;       // 30秒
const uint32_t Config::HEALTH_CHECK_INTERVAL = 60000; // 60秒

// 时间同步配置
const uint32_t Config::TIME_SYNC_INTERVAL_MS = 2000;        // 2秒
const uint8_t Config::TIME_SYNC_CALC_COUNT = 3;             // 每个传感器计算3次
const uint32_t Config::TIME_SYNC_CALC_INTERVAL_MS = 2000;   // 2秒计算一次

// 设备配置
const char* Config::DEVICE_CODE = "2025001";
const char* Config::FIRMWARE_VERSION = "V3.3";

// 数据包配置
const char* Config::SENSOR_DATA_PACKET_TYPE = "batch_sensor_data";

// 调试配置
bool Config::SHOW_DROPPED_PACKETS = false;
bool Config::DEBUG_PPRINT = false;

void Config::printConfig() {
    Serial.printf("\n=== 系统配置 ===\n");
    Serial.printf("固件版本: %s\n", FIRMWARE_VERSION);
    Serial.printf("设备编码: %s\n", DEVICE_CODE);
    Serial.printf("\n网络配置:\n");
    Serial.printf("  WiFi SSID: %s\n", WIFI_SSID);
    Serial.printf("  服务器地址: %s:%d\n", SERVER_URL, SERVER_PORT);
    Serial.printf("  WebSocket路径: %s%s/\n", WEBSOCKET_PATH, DEVICE_CODE);
    Serial.printf("  数据包类型: %s\n", SENSOR_DATA_PACKET_TYPE);
    Serial.printf("\nUART配置:\n");
    Serial.printf("  波特率: %d\n", UART_BAUD_RATE);
    Serial.printf("  UART1: TX=%d, RX=%d\n", UART_TX_PIN, UART_RX_PIN);
    Serial.printf("\n缓冲区配置:\n");
    Serial.printf("  环形缓冲区大小: %d bytes\n", RING_BUFFER_SIZE);
    Serial.printf("  块池大小: %d blocks\n", BLOCK_POOL_SIZE);
    Serial.printf("  每块最大帧数: %d\n", MAX_FRAMES_PER_BLOCK);
    Serial.printf("\n任务配置:\n");
    Serial.printf("  UART任务: 栈大小=%d, 优先级=%d\n", UART_TASK_STACK_SIZE, UART_TASK_PRIORITY);
    Serial.printf("  网络任务: 栈大小=%d, 优先级=%d\n", NETWORK_TASK_STACK_SIZE, NETWORK_TASK_PRIORITY);
    Serial.printf("  CLI任务: 栈大小=%d, 优先级=%d\n", CLI_TASK_STACK_SIZE, CLI_TASK_PRIORITY);
    Serial.printf("  监控任务: 栈大小=%d, 优先级=%d\n", MONITOR_TASK_STACK_SIZE, MONITOR_TASK_PRIORITY);
    Serial.printf("\n时间配置:\n");
    Serial.printf("  心跳间隔: %d ms\n", HEARTBEAT_INTERVAL);
    Serial.printf("  状态间隔: %d ms\n", STATUS_INTERVAL);
    Serial.printf("  健康检查间隔: %d ms\n", HEALTH_CHECK_INTERVAL);
    Serial.printf("\n调试配置:\n");
    Serial.printf("  显示丢弃数据包: %s\n", SHOW_DROPPED_PACKETS ? "开启" : "关闭");
    Serial.printf("================\n\n");
}

bool Config::validateConfig() {
    bool valid = true;
    
    // 验证网络配置
    if (strlen(WIFI_SSID) == 0) {
        Serial.printf("[Config] ERROR: WiFi SSID not configured\n");
        valid = false;
    }
    
    if (strlen(WIFI_PASSWORD) == 0) {
        Serial.printf("[Config] ERROR: WiFi password not configured\n");
        valid = false;
    }
    
    if (strlen(SERVER_URL) == 0) {
        Serial.printf("[Config] ERROR: Server URL not configured\n");
        valid = false;
    }
    
    if (SERVER_PORT == 0) {
        Serial.printf("[Config] ERROR: Server port not configured\n");
        valid = false;
    }
    
    // 验证UART配置
    if (UART_BAUD_RATE == 0) {
        Serial.printf("[Config] ERROR: UART baud rate not configured\n");
        valid = false;
    }
    
    // 验证缓冲区配置
    if (RING_BUFFER_SIZE == 0) {
        Serial.printf("[Config] ERROR: Ring buffer size not configured\n");
        valid = false;
    }
    
    if (BLOCK_POOL_SIZE == 0) {
        Serial.printf("[Config] ERROR: Block pool size not configured\n");
        valid = false;
    }
    
    if (MAX_FRAMES_PER_BLOCK == 0) {
        Serial.printf("[Config] ERROR: Max frames per block not configured\n");
        valid = false;
    }
    
    // 验证任务配置
    if (UART_TASK_STACK_SIZE < 1024) {
        Serial.printf("[Config] WARNING: UART task stack size too small\n");
    }
    
    if (NETWORK_TASK_STACK_SIZE < 2048) {
        Serial.printf("[Config] WARNING: Network task stack size too small\n");
    }
    
    if (valid) {
        Serial.printf("[Config] Configuration validation passed\n");
    } else {
        Serial.printf("[Config] Configuration validation failed\n");
    }
    
    return valid;
}
