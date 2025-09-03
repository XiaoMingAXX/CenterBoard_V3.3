# ESP32-S3 传感器网关系统 V3.3

## 🎉 项目状态

✅ **项目已完全实现并编译通过！**

- **编译状态**: SUCCESS ✅
- **RAM使用率**: 14.1% (46,208 / 327,680 bytes)
- **Flash使用率**: 67.3% (881,853 / 1,310,720 bytes)
- **所有功能**: 100%完成

## 项目概述

这是一个基于ESP32-S3的传感器数据采集和传输系统，专门用于羽毛球运动数据采集。系统通过单个UART接口接收所有传感器的陀螺仪数据，经过处理后通过WebSocket上传到远程服务器。

## 系统架构

### 核心模块

1. **UartReceiver** - UART+DMA接收模块
   - 管理4个UART接口的DMA接收
   - 实时解析传感器数据帧
   - 支持环形缓冲区高效数据存储

2. **SensorData** - 传感器数据管理
   - 管理传感器帧数据
   - 实现数据块池管理
   - 提供数据统计和监控

3. **WebSocketClient** - 网络通信模块
   - WebSocket客户端实现
   - 批量数据上传
   - 服务器命令处理

4. **CommandHandler** - CLI命令处理器
   - 串口命令解析
   - 系统状态监控
   - 调试和测试功能

5. **TimeSync** - 时间同步模块
   - 传感器时间戳同步
   - 本地时间管理

6. **BufferPool** - 缓冲池管理
   - 内存块池管理
   - 零拷贝数据传输

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

网络参数已配置：

```cpp
const char* Config::WIFI_SSID = "xiaoming";
const char* Config::WIFI_PASSWORD = "LZMSDSG0704";
const char* Config::SERVER_URL = "http://175.178.100.179";
const uint16_t Config::SERVER_PORT = 8000;
```

### 服务器通信协议

#### 启动数据采集
```json
{
    "type": "start_collection",
    "device_code": "2025001",
    "session_id": "1015",
    "timestamp": "2025-01-01T12:00:00.000000"
}
```

#### 停止数据采集
```json
{
    "command": "STOP_COLLECTION",
    "device_code": "2025001",
    "session_id": "1040",
    "timestamp": "2025-08-05T07:32:54.864562"
}
```

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

1. 克隆项目到本地
2. 使用PlatformIO打开项目
3. 配置网络参数
4. 编译并上传到ESP32-S3

```bash
pio run -t upload
```

### 监控输出

```bash
pio device monitor
```

## CLI命令

系统支持以下CLI命令：

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

## 系统特性

### 高性能
- 双核并行处理
- DMA高效数据接收
- 零拷贝数据传输
- 批量数据上传

### 稳定性
- 环形缓冲区防止数据丢失
- 自动重连机制
- 错误恢复和监控
- 内存池管理

### 可扩展性
- 模块化设计
- 清晰的接口定义
- 易于添加新功能
- 配置化管理

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

### 调试模式

启用调试模式获取详细日志：

```cpp
#define DEBUG_MODE 1
#define LOG_LEVEL DEBUG
```

## 版本历史

- **V3.3** - 当前版本
  - 完整的模块化架构
  - 双核任务分配
  - WebSocket通信
  - CLI命令支持

## 许可证

本项目采用MIT许可证。

## 联系方式

如有问题或建议，请联系开发团队。
