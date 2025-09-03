# ESP32-S3 传感器网关系统 V3.3

## 🎉 项目状态

✅ **项目已完全实现并编译通过！**

- **编译状态**: SUCCESS ✅
- **RAM使用率**: 14.1% (46,264 / 327,680 bytes)
- **Flash使用率**: 42.1% (883,357 / 1,310,720 bytes)
- **所有功能**: 100%完成

## 项目概述

这是一个基于ESP32-S3的传感器数据采集和传输系统，专门用于羽毛球运动数据采集。系统通过单个UART接口接收所有传感器的陀螺仪数据，经过处理后通过WebSocket上传到远程服务器。

## 系统架构

### 核心模块

1. **UartReceiver** - UART+DMA接收模块
   - 管理单个UART接口的DMA接收
   - 实时解析传感器数据帧
   - 支持中断驱动的数据处理

2. **SensorData** - 传感器数据管理
   - 管理传感器帧数据
   - 实现数据块队列管理
   - 提供数据统计和监控

3. **WebSocketClient** - 网络通信模块
   - WebSocket客户端实现
   - 批量数据上传
   - 服务器命令处理

4. **CommandHandler** - CLI命令处理器
   - 串口命令解析
   - 系统状态监控
   - 调试和测试功能

5. **TaskManager** - 任务管理器
   - FreeRTOS任务调度
   - 双核任务分配
   - 系统初始化

### 任务分配

- **Core 0**: UART接收任务
- **Core 1**: 网络任务、CLI任务、监控任务

## 硬件配置

### ESP32-S3 引脚配置

| 接口 | TX引脚 | RX引脚 | 波特率 | 说明 |
|------|--------|--------|--------|------|
| UART1 | 17 | 16 | 460800 | 接收所有传感器数据 |

### 传感器ID映射

| 传感器ID | 传感器类型 | 说明 |
|----------|------------|------|
| 1 | waist | 腰部传感器 |
| 2 | shoulder | 肩部传感器 |
| 3 | wrist | 手腕传感器 |
| 4 | racket | 球拍传感器 |

### 传感器数据格式

```
帧结构 (43字节):
- 帧头: 0xAA (1字节)
- 时间戳: uint32_t (4字节)
- 加速度: float[3] (12字节)
- 角速度: float[3] (12字节)
- 角度: float[3] (12字节)
- 传感器ID: uint8_t (1字节)
- 帧尾: 0x55 (1字节)
```

## 软件配置

### 网络配置

```cpp
const char* Config::WIFI_SSID = "xiaoming";
const char* Config::WIFI_PASSWORD = "LZMSDSG0704";
const char* Config::SERVER_URL = "175.178.100.179";
const uint16_t Config::SERVER_PORT = 8000;
const char* Config::WEBSOCKET_PATH = "/wxapp/esp32/batch_upload/";
```

### 服务器通信协议

#### 数据上传格式
```json
{
    "batch_data": [
        {
            "acc": [x, y, z],
            "gyro": [x, y, z],
            "angle": [x, y, z],
            "timestamp": 1234567890
        }
    ],
    "device_code": "2025001",
    "sensor_type": "waist",
    "session_id": "1015"
}
```

## 编译和运行

### 环境要求

- PlatformIO IDE
- ESP32-S3开发板
- Arduino框架

### 编译步骤

```bash
pio run -t upload
```

### 监控输出

```bash
pio device monitor
```

## CLI命令

| 命令 | 描述 | 示例 |
|------|------|------|
| `help` | 显示帮助信息 | `help` |
| `status` | 显示系统状态 | `status` |
| `start` | 开始数据采集 | `start` |
| `stop` | 停止数据采集 | `stop` |
| `device` | 设置设备信息 | `device 2025001 1015` |
| `test` | 测试网络连接 | `test` |
| `stats` | 显示统计信息 | `stats` |
| `reset` | 重置统计信息 | `reset` |
| `config` | 显示配置信息 | `config` |
| `dropped` | 切换显示丢弃数据包 | `dropped` |

## 系统特性

### 高性能
- 双核并行处理
- DMA+中断高效数据接收
- 零拷贝数据传输
- 批量数据上传
- FIFO队列丢弃策略

### 稳定性
- 环形缓冲区防止数据丢失
- 自动重连机制
- 错误恢复和监控
- 内存池管理
- 双重释放保护

### 可扩展性
- 模块化设计
- 清晰的接口定义
- 易于添加新功能
- 配置化管理

## 主要修复

### V3.3.1 修复内容

1. **UART中断注册问题** - 修复ESP32-S3 UART ISR注册失败
2. **Core Dump分区错误** - 添加自定义分区表
3. **CLI命令无响应** - 修复CLI任务栈溢出和响应问题
4. **传感器数据流中断** - 修复UartReceiver和TaskManager的SensorData实例冲突
5. **WebSocket队列内存泄漏** - 修复未连接时的内存泄漏问题
6. **双重释放内存损坏** - 修复TaskManager中的双重释放问题
7. **FIFO队列丢弃策略** - 实现优先丢弃旧数据的策略
8. **调试功能增强** - 添加dropped命令控制调试信息显示

## 故障排除

### 常见问题

1. **WiFi连接失败**
   - 检查SSID和密码配置
   - 确认网络信号强度

2. **UART数据接收异常**
   - 检查引脚连接
   - 确认波特率设置
   - 验证传感器数据格式

3. **WebSocket连接失败**
   - 检查服务器地址和端口
   - 确认网络连通性
   - 查看防火墙设置

4. **内存损坏错误**
   - 检查是否有双重释放
   - 验证内存分配和释放逻辑

## 版本历史

- **V3.3.1** - 当前版本
  - 修复所有已知问题
  - 优化内存管理
  - 增强调试功能
  - 提高系统稳定性

- **V3.3** - 基础版本
  - 完整的模块化架构
  - 双核任务分配
  - WebSocket通信
  - CLI命令支持

## 许可证

本项目采用MIT许可证。

## 联系方式

如有问题或建议，请联系开发团队。