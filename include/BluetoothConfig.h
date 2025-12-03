#ifndef BLUETOOTH_CONFIG_H
#define BLUETOOTH_CONFIG_H

#include <Arduino.h>
#include "driver/uart.h"

// 前向声明
class UartReceiver;

// LED状态枚举
enum class LEDState {
    OFF,           // 关闭
    ON,            // 常亮
    SLOW_BLINK,    // 慢闪 (1Hz) - 扫描中
    FAST_BLINK     // 快闪 (5Hz) - 已扫描到
};

// 蓝牙设备连接状态
enum class DeviceConnectionState {
    DISCONNECTED,   // 未连接
    SCANNING,       // 扫描中
    SCANNED,        // 已扫描到
    CONNECTING,     // 连接中
    CONNECTED       // 已连接
};

// 蓝牙设备信息
struct BluetoothDevice {
    const char* macAddress;          // MAC地址
    DeviceConnectionState state;     // 连接状态
    int scanIndex;                   // 扫描列表中的序号（-1表示未扫描到）
    uint32_t lastUpdateTime;         // 最后更新时间
};

// 按钮状态结构
struct ButtonState {
    uint8_t pin;
    bool lastState;
    uint32_t lastDebounceTime;
    bool pressed;
};

// LED状态结构
struct LEDControl {
    uint8_t pin;
    LEDState state;
    uint32_t lastToggleTime;
    bool currentLevel;
};

class BluetoothConfig {
public:
    BluetoothConfig();
    ~BluetoothConfig();
    
    // 初始化
    bool initialize();
    
    // 主循环处理（仅处理按钮和LED）
    void loop();
    
    // 设置配置模式（由CommandHandler调用）
    void setConfigMode(bool enabled);
    
    // 转发来自Serial0的数据到蓝牙模块（由CommandHandler调用）
    void forwardSerialData(const uint8_t* data, size_t length);
    void forwardSerialData(const String& data);
    
    // 写入原始UART数据到环形缓冲区（由UartReceiver使用DMA快速复制）
    void writeUartDataToBuffer(const uint8_t* data, size_t length);
    
    // 从环形缓冲区读取并分析配置信息（在loop中调用）
    void readAndParseConfigData();
    
    // 处理按钮和LED
    void handleButtonsAndLEDs();
    
    // 蓝牙业务逻辑
    void handleBluetoothBusiness();
    
    // 处理从UART接收的蓝牙事件（连接/断开通知）
    void processBluetoothEvent(const String& data);
    
    // 获取配置模式状态
    bool isConfigMode() const { return configMode; }
    
    // 设置UartReceiver实例（用于获取帧数统计）
    void setUartReceiver(UartReceiver* receiver);
    
    // 测试接口：手动设置LED状态（用于调试）
    void testSetLED(uint8_t index, LEDState state);
    
    // 测试接口：读取按钮状态（用于调试）
    bool testReadButton(uint8_t index);
    
private:
    // 配置模式标志
    bool configMode;
    
    // UartReceiver实例（用于获取帧数统计）
    UartReceiver* uartReceiver;
    
    // UART原始数据接收环形缓冲区（由UartReceiver使用memcpy快速复制）
    static const size_t UART_RX_BUFFER_SIZE = 4096;
    uint8_t uartRxBuffer[UART_RX_BUFFER_SIZE];
    volatile size_t writePos;   // 写指针（由UartReceiver写入）
    volatile size_t readPos;    // 读指针（由BluetoothConfig读取）
    SemaphoreHandle_t bufferMutex;  // 互斥锁保护环形缓冲区
    
    // 配置信息解析临时缓冲区（用于累积一行完整的配置信息）
    static const size_t CONFIG_LINE_BUFFER_SIZE = 512;
    uint8_t configLineBuffer[CONFIG_LINE_BUFFER_SIZE];
    size_t configLineBufferPos;
    
    // BLE数据包识别（用于过滤）
    static const uint8_t BLE_DATA_HEADER[10];
    static const uint8_t BLE_DATA_FOOTER[16];
    static const size_t BLE_PACKET_SIZE = 69;
    
    // 蓝牙设备管理
    static const uint8_t DEVICE_COUNT = 3;
    BluetoothDevice devices[DEVICE_COUNT];
    
    // ========== 可配置参数（便于调试） ==========
    // 扫描参数
    static const uint32_t BT_SCAN_DURATION_SEC = 5;           // 蓝牙扫描持续时间（秒）
    static const uint32_t SCAN_TIMEOUT_MS = 7000;             // 扫描超时（扫描时间+容错，ms）
    static const uint32_t SCAN_DATA_TIMEOUT_MS = 1000;        // 扫描数据接收超时（ms）
    
    // 自动连接参数
    static const uint32_t AUTO_CONNECT_START_DELAY_MS = 10000;  // 开机后延迟启动时间（ms）
    static const uint32_t AUTO_SCAN_INTERVAL_MS = 8000;         // 自动扫描间隔（ms）
    static const uint8_t MAX_AUTO_SCAN_COUNT = 5;               // 最多自动扫描次数
    
    // 连接参数
    static const uint32_t CONNECT_RETRY_INTERVAL_MS = 1000;  // 连接重试间隔（ms）
    static const uint8_t MAX_CONNECT_RETRY_COUNT = 5;        // 每个设备最多连接尝试次数
    static const uint32_t CONNECT_WAIT_TIMEOUT_MS = 3000;    // 等待连接成功的超时时间（ms）
    
    // 扫描状态
    bool isScanning;
    uint32_t scanStartTime;
    uint32_t lastScanDataTime;  // 最后接收扫描数据的时间
    String scanResultBuffer;  // 累积扫描结果
    
    // 自动连接流程状态
    enum class AutoConnectState {
        IDLE,           // 空闲
        WAITING,        // 等待开机延迟
        SCANNING,       // 正在扫描
        CONNECTING,     // 正在连接设备
        COMPLETED       // 完成
    };
    
    AutoConnectState autoConnectState;  // 自动连接状态
    uint32_t systemStartTime;           // 系统启动时间
    uint32_t lastAutoScanTime;          // 最后一次自动扫描时间
    uint8_t autoScanCount;              // 自动扫描次数
    
    // 连接流程状态
    int8_t currentConnectingDevice;     // 当前正在连接的设备索引（-1表示无）
    uint8_t connectRetryCount;          // 当前设备的连接重试次数
    uint32_t lastConnectAttemptTime;    // 最后一次连接尝试时间
    uint32_t connectStartTime;          // 开始连接当前设备的时间
    
    // 基于帧数的连接检测
    uint32_t lastFrameCounts[DEVICE_COUNT];  // 上次检查的帧数
    uint32_t lastConnectionCheckTime;        // 上次检查连接的时间
    static const uint32_t CONNECTION_CHECK_INTERVAL_MS = 500;  // 每500ms检查一次连接
    static const uint32_t FRAME_INCREASE_THRESHOLD = 3;  // 帧数增加阈值，表示设备在发送数据（500ms内至少3帧）
    
    // 待连接设备列表
    int pendingConnectDevices[DEVICE_COUNT];  // 待连接设备的索引列表
    int pendingConnectCount;                   // 待连接设备数量
    
    // 按钮配置
    static const uint8_t BUTTON_COUNT = 3;
    ButtonState buttons[BUTTON_COUNT];
    static const uint8_t BUTTON_PINS[BUTTON_COUNT];
    static const uint32_t DEBOUNCE_TIME_MS = 2;
    
    // LED配置
    static const uint8_t LED_COUNT = 3;
    LEDControl leds[LED_COUNT];
    static const uint8_t LED_PINS[LED_COUNT];
    static const uint32_t SLOW_BLINK_INTERVAL_MS = 500;  // 1Hz = 500ms on, 500ms off
    static const uint32_t FAST_BLINK_INTERVAL_MS = 100;  // 5Hz = 100ms on, 100ms off
    
    // 初始化GPIO
    void initGPIO();
    
    // 按钮处理
    void updateButton(uint8_t index);
    void handleButtonPress(uint8_t buttonIndex);
    
    // LED处理
    void updateLED(uint8_t index);
    void setLEDState(uint8_t index, LEDState newState);
    void cycleLEDState(uint8_t index);
    
    // 配置信息解析
    void processConfigLine(const String& line);
    bool isBleDataPacket(const uint8_t* data, size_t length);
    size_t getAvailableDataCount() const;  // 获取环形缓冲区中可读数据量
    
    // 蓝牙业务逻辑内部函数
    void startScan();
    void processScanResult(const String& result);
    void connectDevice(uint8_t deviceIndex);
    void updateDeviceState(uint8_t deviceIndex, DeviceConnectionState newState);
    void updateLEDByDeviceState(uint8_t deviceIndex);
    void handleButtonPressForDevice(uint8_t deviceIndex);
    void sendATCommand(const String& command);
    bool parseMAC(const String& line, String& mac, int& index);
    
    // 自动连接流程
    void autoConnectProcess();
    void startAutoConnect();
    bool allTargetDevicesScanned();  // 检查是否扫到所有目标设备
    void startConnectingDevices();   // 开始连接设备流程
    void processConnecting();        // 处理连接流程
    
    // 基于帧数检测连接状态
    void checkConnectionByFrameCount();
};

#endif // BLUETOOTH_CONFIG_H

