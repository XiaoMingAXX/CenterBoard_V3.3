#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// 系统配置类
class Config {
public:
    // 网络配置
    static const char* WIFI_SSID;
    static const char* WIFI_PASSWORD;
    static const char* SERVER_URL;
    static const uint16_t SERVER_PORT;
    static const char* WEBSOCKET_PATH;
    
    // UART配置
    static const uint32_t UART_BAUD_RATE;
    static const int UART_TX_PIN;
    static const int UART_RX_PIN;
    
    // 缓冲区配置
    static const size_t RING_BUFFER_SIZE;
    static const size_t BLOCK_POOL_SIZE;
    static const size_t MAX_FRAMES_PER_BLOCK;
    
    // 任务配置
    static const uint32_t UART_TASK_STACK_SIZE;
    static const uint32_t NETWORK_TASK_STACK_SIZE;
    static const uint32_t CLI_TASK_STACK_SIZE;
    static const uint32_t MONITOR_TASK_STACK_SIZE;
    
    static const uint32_t UART_TASK_PRIORITY;
    static const uint32_t NETWORK_TASK_PRIORITY;
    static const uint32_t CLI_TASK_PRIORITY;
    static const uint32_t MONITOR_TASK_PRIORITY;
    
    // 传感器配置
    static const uint8_t SENSOR_COUNT;
    static const uint8_t FRAME_SIZE;
    
    // 时间配置
    static const uint32_t HEARTBEAT_INTERVAL;
    static const uint32_t STATUS_INTERVAL;
    static const uint32_t HEALTH_CHECK_INTERVAL;
    
    // 设备配置
    static const char* DEVICE_CODE;
    static const char* FIRMWARE_VERSION;
    
    // 数据包配置
    static const char* SENSOR_DATA_PACKET_TYPE;
    
    // 调试配置
    static bool SHOW_DROPPED_PACKETS;
    static bool DEBUG_PPRINT;
    
    // 获取配置信息
    static void printConfig();
    
    // 验证配置
    static bool validateConfig();
};

#endif // CONFIG_H
