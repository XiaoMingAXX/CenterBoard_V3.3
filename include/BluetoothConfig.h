#ifndef BLUETOOTH_CONFIG_H
#define BLUETOOTH_CONFIG_H

#include <Arduino.h>
#include "driver/uart.h"

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
    
    // 转发来自UART的配置信息到Serial0（由UartReceiver调用）
    void forwardUartData(const uint8_t* data, size_t length);
    
    // 处理按钮和LED
    void handleButtonsAndLEDs();
    
    // 蓝牙业务逻辑
    void handleBluetoothBusiness();
    
    // 处理从UART接收的蓝牙事件（连接/断开通知）
    void processBluetoothEvent(const String& data);
    
    // 获取配置模式状态
    bool isConfigMode() const { return configMode; }
    
    // 测试接口：手动设置LED状态（用于调试）
    void testSetLED(uint8_t index, LEDState state);
    
    // 测试接口：读取按钮状态（用于调试）
    bool testReadButton(uint8_t index);
    
private:
    // 配置模式标志
    bool configMode;
    
    // 蓝牙数据缓冲区（用于检测数据包头尾）
    uint8_t bleBuffer[256];
    size_t bleBufferPos;
    
    // 蓝牙设备管理
    static const uint8_t DEVICE_COUNT = 3;
    BluetoothDevice devices[DEVICE_COUNT];
    
    // 扫描状态
    bool isScanning;
    uint32_t scanStartTime;
    uint32_t lastScanDataTime;  // 最后接收扫描数据的时间
    String scanResultBuffer;  // 累积扫描结果
    static const uint32_t SCAN_TIMEOUT_MS = 3000;  // 扫描超时（2秒扫描+1秒容错）
    static const uint32_t SCAN_DATA_TIMEOUT_MS = 500;  // 扫描数据接收超时（500ms无新数据则认为完成）
    
    // 自动连接
    bool autoConnectStarted;      // 是否已开始自动连接流程
    uint32_t systemStartTime;     // 系统启动时间
    uint32_t lastAutoScanTime;    // 最后一次自动扫描时间
    uint8_t autoScanCount;        // 自动扫描次数
    static const uint32_t AUTO_CONNECT_START_DELAY_MS = 10000;  // 启动10秒后开始自动连接
    static const uint32_t AUTO_SCAN_INTERVAL_MS = 3000;         // 每隔3秒扫描一次
    static const uint8_t MAX_AUTO_SCAN_COUNT = 5;               // 最多扫描5次
    
    // 连接队列
    int connectQueue[DEVICE_COUNT];  // 待连接设备的索引
    int connectQueueSize;
    
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
    
    // 蓝牙数据包特征
    static const uint8_t BLE_DATA_HEADER[10];
    static const uint8_t BLE_DATA_FOOTER[16];
    static const size_t BLE_PACKET_SIZE = 69;  // 10 (header) + 43 (data) + 16 (footer)
    
    // 初始化GPIO
    void initGPIO();
    
    // 按钮处理
    void updateButton(uint8_t index);
    void handleButtonPress(uint8_t buttonIndex);
    
    // LED处理
    void updateLED(uint8_t index);
    void setLEDState(uint8_t index, LEDState newState);
    void cycleLEDState(uint8_t index);
    
    // 蓝牙业务逻辑内部函数
    void startScan();
    void processScanResult(const String& result);
    void connectDevice(uint8_t deviceIndex);
    void updateDeviceState(uint8_t deviceIndex, DeviceConnectionState newState);
    void updateLEDByDeviceState(uint8_t deviceIndex);
    void handleButtonPressForDevice(uint8_t deviceIndex);
    void sendATCommand(const String& command);
    bool parseMAC(const String& line, String& mac, int& index);
};

#endif // BLUETOOTH_CONFIG_H

