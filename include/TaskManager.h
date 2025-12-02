#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include "UartReceiver.h"
#include "WebSocketClient.h"
#include "CommandHandler.h"
#include "TimeSync.h"
#include "BufferPool.h"
#include "BluetoothConfig.h"

// 任务管理器类
class TaskManager {
public:
    TaskManager();
    ~TaskManager();
    
    // 初始化任务管理器
    bool initialize();
    
    // 启动所有任务
    bool startTasks();
    
    // 停止所有任务
    void stopTasks();
    
    // 获取系统状态
    void getSystemStatus();
    
private:
    // 模块实例
    UartReceiver* uartReceiver;
    WebSocketClient* webSocketClient;
    CommandHandler* commandHandler;
    TimeSync* timeSync;
    BufferPool* bufferPool;
    SensorData* sensorData;
    BluetoothConfig* bluetoothConfig;
    
    // 任务句柄
    TaskHandle_t uartTaskHandle;
    TaskHandle_t networkTaskHandle;
    TaskHandle_t cliTaskHandle;
    TaskHandle_t monitorTaskHandle;
    TaskHandle_t timeSyncTaskHandle;
    TaskHandle_t bluetoothConfigTaskHandle;
    
    // 任务函数
    static void uartTask(void* parameter);
    static void networkTask(void* parameter);
    static void cliTask(void* parameter);
    static void monitorTask(void* parameter);
    static void timeSyncTask(void* parameter);
    static void bluetoothConfigTask(void* parameter);
    
    // 任务配置
    static const uint32_t UART_TASK_STACK_SIZE = 4096;
    static const uint32_t NETWORK_TASK_STACK_SIZE = 8192;
    static const uint32_t CLI_TASK_STACK_SIZE = 4096;  // 增加CLI任务栈大小
    static const uint32_t MONITOR_TASK_STACK_SIZE = 2048;
    static const uint32_t TIME_SYNC_TASK_STACK_SIZE = 4096;
    static const uint32_t BLUETOOTH_CONFIG_TASK_STACK_SIZE = 4096;
    
    static const uint32_t UART_TASK_PRIORITY = 3;
    static const uint32_t NETWORK_TASK_PRIORITY = 2;
    static const uint32_t CLI_TASK_PRIORITY = 1;
    static const uint32_t MONITOR_TASK_PRIORITY = 1;
    static const uint32_t TIME_SYNC_TASK_PRIORITY = 1;
    static const uint32_t BLUETOOTH_CONFIG_TASK_PRIORITY = 2;  // 较高优先级，保证配置模式响应及时
    
    // 任务状态
    bool tasksRunning;
    
    // 创建任务
    bool createUartTask();
    bool createNetworkTask();
    bool createCliTask();
    bool createMonitorTask();
    bool createTimeSyncTask();
    bool createBluetoothConfigTask();
    
    // 任务循环
    void uartTaskLoop();
    void networkTaskLoop();
    void cliTaskLoop();
    void monitorTaskLoop();
    void timeSyncTaskLoop();
    void bluetoothConfigTaskLoop();
};

#endif // TASK_MANAGER_H
