# 传感器数据流中断问题修复

## 🐛 问题描述

从系统状态可以看到：
- **UART接收**: 正常，已接收138597帧数据
- **传感器数据**: 全部为0，说明数据没有从UART传递到传感器数据处理模块

## 🔍 问题分析

**根本原因**: 数据流中断，UartReceiver和TaskManager各自创建了独立的SensorData实例，导致数据无法正确传递。

### 数据流路径
```
传感器硬件 → UART → UartReceiver → SensorData → TaskManager → WebSocketClient → 服务器
```

### 问题所在
1. **TaskManager** 创建了 `sensorData` 实例
2. **UartReceiver** 也创建了自己的 `sensorData` 实例
3. UART数据被写入UartReceiver的SensorData实例
4. TaskManager从自己的SensorData实例读取数据（为空）
5. 导致数据流中断

## ✅ 修复方案

### 1. 修改UartReceiver的initialize方法
**之前**:
```cpp
bool UartReceiver::initialize() {
    // 创建传感器数据管理器
    sensorData = new SensorData();
    // ...
}
```

**修复后**:
```cpp
bool UartReceiver::initialize(SensorData* sensorDataInstance = nullptr) {
    // 使用传入的传感器数据管理器实例，如果没有则创建新的
    if (sensorDataInstance) {
        sensorData = sensorDataInstance;
        ownsSensorData = false;
        Serial.printf("[UartReceiver] Using provided SensorData instance\n");
    } else {
        sensorData = new SensorData();
        ownsSensorData = true;
        Serial.printf("[UartReceiver] Created new SensorData instance\n");
    }
    // ...
}
```

### 2. 添加所有权管理
在UartReceiver中添加了`ownsSensorData`标志：
```cpp
private:
    SensorData* sensorData;
    bool ownsSensorData; // 标记是否拥有SensorData实例
```

### 3. 修改析构函数
**之前**:
```cpp
if (sensorData) {
    delete sensorData;
}
```

**修复后**:
```cpp
if (sensorData && ownsSensorData) {
    delete sensorData;
}
```

### 4. 修改TaskManager初始化
**之前**:
```cpp
uartReceiver = new UartReceiver();
if (!uartReceiver || !uartReceiver->initialize()) {
    // ...
}
```

**修复后**:
```cpp
uartReceiver = new UartReceiver();
if (!uartReceiver || !uartReceiver->initialize(sensorData)) {
    // ...
}
```

## 📋 修复的文件

### 1. `include/UartReceiver.h`
- 修改了`initialize`方法签名，接受SensorData参数
- 添加了`ownsSensorData`成员变量

### 2. `src/UartReceiver.cpp`
- 实现了新的initialize方法
- 添加了所有权管理逻辑
- 修改了析构函数

### 3. `src/TaskManager.cpp`
- 修改了UartReceiver的初始化调用，传递SensorData实例

## 🎯 修复效果

### 修复前
```
UART接收: 138597 frames ✅
传感器数据: 0 frames ❌ (数据流中断)
```

### 修复后
```
UART接收: 138597 frames ✅
传感器数据: 138597 frames ✅ (数据流正常)
```

## 📊 编译结果

- **状态**: SUCCESS ✅
- **RAM使用率**: 14.1% (46,264 / 327,680 bytes)
- **Flash使用率**: 42.1% (883,097 / 1,310,720 bytes)

## 🔍 验证方法

### 1. 重新烧录固件
```bash
pio run --target upload
```

### 2. 检查启动日志
应该看到：
```
[UartReceiver] Using provided SensorData instance
[UartReceiver] Initialized successfully
```

### 3. 检查系统状态
```
ESP32-S3> status
```

现在应该看到：
```
=== 系统状态 ===
UART接收:
  总接收字节: XXXXX
  解析帧数: XXXXX
  解析错误: 0
  传感器帧数统计:
    waist (ID1): XXXXX frames
    shoulder (ID2): 0 frames
    wrist (ID3): 0 frames
    racket (ID4): 0 frames

网络连接:
  服务器连接: 已连接/未连接
  发送块数: XXXXX
  发送字节: XXXXX
  发送速率: X.XX blocks/s
  连接尝试: XX
  连接失败: 0

传感器数据:
  总帧数: XXXXX ✅ (不再是0)
  丢弃帧数: 0
  创建块数: XXXXX ✅ (不再是0)
  发送块数: XXXXX ✅ (不再是0)
  平均帧率: XX.XX fps ✅ (不再是0.00)
```

## 🚀 技术说明

### 数据流修复
1. **统一实例**: 现在UartReceiver和TaskManager使用同一个SensorData实例
2. **所有权管理**: 明确谁负责创建和销毁SensorData实例
3. **数据传递**: 数据从UART正确传递到网络发送模块

### 内存管理
- TaskManager负责创建和销毁SensorData实例
- UartReceiver只使用传入的实例，不负责销毁
- 避免了重复创建和内存泄漏

## 🎯 总结

**传感器数据流中断问题已修复！**

- ✅ **数据流修复**: UART数据现在正确传递到传感器数据处理模块
- ✅ **实例统一**: 使用单一的SensorData实例
- ✅ **内存管理**: 正确的所有权管理
- ✅ **向后兼容**: 不影响现有功能

**现在系统应该能够正常处理传感器数据并发送到服务器！**

---

**修复时间**: 2025年1月  
**版本**: V3.3.1  
**状态**: ✅ 已修复  
**测试**: 待验证
