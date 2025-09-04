#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include "UartReceiver.h"
#include "WebSocketClient.h"
#include "SensorData.h"

// CLI命令处理器类
class CommandHandler {
public:
    CommandHandler();
    ~CommandHandler();
    
    // 初始化命令处理器
    bool initialize(UartReceiver* receiver, WebSocketClient* client, SensorData* sensorData);
    
    // 处理输入命令
    void processCommand(const String& command);
    
    // 显示帮助信息
    void showHelp(const String& args = "");
    
    // 显示系统状态
    void showStatus(const String& args = "");
    
    // 显示传感器数据
    void showSensorData(const String& args = "");
    
    // 测试网络连接
    void testNetwork(const String& args = "");
    
    // 执行时间同步
    void syncTime(const String& args = "");
    
    // 设置批量大小
    void setBatchSize(const String& args);
    
    // 开始数据采集
    void startCollection(const String& args = "");
    
    // 停止数据采集
    void stopCollection(const String& args = "");
    
    // 重置统计信息
    void resetStats(const String& args = "");
    
    // 显示统计信息
    void showStats(const String& args = "");
    
    // 设置设备信息
    void setDeviceInfo(const String& args);
    
    // 测试UART接收
    void testUart(const String& args = "");
    
    // 显示缓冲区状态
    void showBufferStatus(const String& args = "");
    
    // 实时显示传感器数据
    void showRealtimeData(const String& args = "");
    
    // 检查是否启用实时显示
    static bool isRealtimeDataEnabled();
    
    // 显示实时传感器数据（由UartReceiver调用）
    static void displayRealtimeSensorData(const SensorFrame& frame);
    
private:
    UartReceiver* uartReceiver;
    WebSocketClient* webSocketClient;
    SensorData* sensorData;
    
    // 实时数据显示控制
    static bool realtimeDataEnabled;
    static uint32_t lastRealtimeDataTime;
    
    // 命令解析相关
    struct Command {
        String name;
        String description;
        void (CommandHandler::*handler)(const String& args);
    };
    
    static const Command commands[];
    static const size_t COMMAND_COUNT = 18;
    
    // 解析命令参数
    String parseCommand(const String& input, String& args);
    
    // 执行命令
    void executeCommand(const String& command, const String& args);
    
    // 显示传感器类型信息
    void showSensorTypes(const String& args = "");
    
    // 显示网络配置
    void showNetworkConfig(const String& args = "");
    
    // 切换显示丢弃数据包
    void toggleDroppedPackets(const String& args = "");

    // 显示Debug信息
    void showDebugInfo(const String& args = "");
    
    // 显示UART配置
    void showUartConfig(const String& args = "");
    
    // 格式化时间戳
    String formatTimestamp(uint32_t timestamp);
    
    // 格式化浮点数
    String formatFloat(float value, int precision = 2);
};

#endif // COMMAND_HANDLER_H
