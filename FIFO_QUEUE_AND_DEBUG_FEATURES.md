# FIFO队列和调试功能实现

## 🎯 实现的功能

1. **FIFO队列丢弃策略** - 发送队列满时优先丢弃旧数据，保持最新数据
2. **CLI调试控制** - 新增`dropped`命令来控制是否显示丢弃数据包的详细信息

## 🔧 修改的文件

### 1. `include/Config.h`
添加了调试配置：
```cpp
// 调试配置
static bool SHOW_DROPPED_PACKETS;
```

### 2. `src/Config.cpp`
添加了调试配置的初始化和显示：
```cpp
// 调试配置
bool Config::SHOW_DROPPED_PACKETS = false;
```

### 3. `src/WebSocketClient.cpp`
修改了发送队列的丢弃策略：
```cpp
bool WebSocketClient::sendDataBlock(DataBlock* block) {
    // 将数据块加入发送队列，如果队列满则丢弃最旧的数据
    if (xQueueSend(sendQueue, &block, 0) != pdTRUE) {
        // 队列满，丢弃最旧的数据块（FIFO）
        DataBlock* oldBlock = nullptr;
        if (xQueueReceive(sendQueue, &oldBlock, 0) == pdTRUE) {
            if (oldBlock) {
                if (Config::SHOW_DROPPED_PACKETS) {
                    Serial.printf("[WebSocketClient] WARNING: Send queue full, dropped old block with %d frames\n", 
                                 oldBlock->frameCount);
                }
                free(oldBlock);
                stats.sendFailures++;
            }
        }
        // 再次尝试添加新数据块...
    }
}
```

### 4. `src/SensorData.cpp`
修改了传感器数据队列的丢弃策略：
```cpp
// 将完整块加入队列，如果队列满则丢弃最旧的数据
if (xQueueSend(blockQueue, &currentBlock, 0) != pdTRUE) {
    // 队列满，丢弃最旧的数据块（FIFO）
    DataBlock* oldBlock = nullptr;
    if (xQueueReceive(blockQueue, &oldBlock, 0) == pdTRUE) {
        if (oldBlock) {
            if (Config::SHOW_DROPPED_PACKETS) {
                Serial.printf("[SensorData] WARNING: Block queue full, dropped old block with %d frames\n", 
                             oldBlock->frameCount);
            }
            stats.droppedFrames += oldBlock->frameCount;
            free(oldBlock);
        }
    }
    // 再次尝试添加新数据块...
}
```

### 5. `include/CommandHandler.h`
添加了新的CLI命令：
```cpp
// 切换显示丢弃数据包
void toggleDroppedPackets(const String& args = "");
```

### 6. `src/CommandHandler.cpp`
实现了新的CLI命令：
```cpp
void CommandHandler::toggleDroppedPackets(const String& args) {
    Config::SHOW_DROPPED_PACKETS = !Config::SHOW_DROPPED_PACKETS;
    Serial.printf("[CommandHandler] 显示丢弃数据包: %s\n", 
                  Config::SHOW_DROPPED_PACKETS ? "开启" : "关闭");
    
    if (Config::SHOW_DROPPED_PACKETS) {
        Serial.printf("现在会显示丢弃数据包的详细信息\n");
    } else {
        Serial.printf("现在不会显示丢弃数据包的详细信息\n");
    }
}
```

## 📋 功能说明

### FIFO队列丢弃策略

**之前的行为**：
- 队列满时直接丢弃新数据
- 可能导致数据不连续，丢失最新信息

**现在的行为**：
- 队列满时丢弃最旧的数据（FIFO）
- 保持队列中的数据始终是最新的
- 确保数据流的连续性和时效性

### CLI调试控制

**新增命令**：`dropped`
- 功能：切换是否显示丢弃数据包的详细信息
- 用法：`dropped`（无需参数）
- 效果：每次执行都会切换显示状态

## 🎯 使用方法

### 1. 查看当前配置
```
ESP32-S3> config
```

会显示：
```
=== 系统配置 ===
固件版本: V3.3
设备编码: 2025001

网络配置:
  WiFi SSID: xiaoming
  服务器地址: 175.178.100.179:8000
  WebSocket路径: /wxapp/esp32/batch_upload/

调试配置:
  显示丢弃数据包: 关闭
```

### 2. 开启丢弃数据包显示
```
ESP32-S3> dropped
[CommandHandler] 显示丢弃数据包: 开启
现在会显示丢弃数据包的详细信息
```

### 3. 关闭丢弃数据包显示
```
ESP32-S3> dropped
[CommandHandler] 显示丢弃数据包: 关闭
现在不会显示丢弃数据包的详细信息
```

### 4. 查看帮助信息
```
ESP32-S3> help
```

会显示包含新命令的帮助信息：
```
=== ESP32-S3 传感器网关 CLI 帮助 ===
可用命令:
  help       - 显示帮助信息
  status     - 显示系统状态
  data       - 显示传感器数据
  test       - 测试网络连接
  sync       - 执行时间同步
  batch      - 设置批量大小
  start      - 开始数据采集
  stop       - 停止数据采集
  reset      - 重置统计信息
  stats      - 显示统计信息
  device     - 设置设备信息
  uart       - 测试UART接收
  buffer     - 显示缓冲区状态
  sensors    - 显示传感器类型
  config     - 显示配置信息
  dropped    - 切换显示丢弃数据包
```

## 📊 编译结果

- **状态**: SUCCESS ✅
- **RAM使用率**: 14.1% (46,264 / 327,680 bytes)
- **Flash使用率**: 42.1% (882,953 / 1,310,720 bytes)

## 🔍 调试信息示例

### 开启显示时
当队列满并丢弃数据时，会显示：
```
[SensorData] WARNING: Block queue full, dropped old block with 50 frames
[WebSocketClient] WARNING: Send queue full, dropped old block with 50 frames
```

### 关闭显示时
不会显示丢弃数据包的详细信息，但统计信息仍然会更新。

## 🚀 优势

### 1. 数据时效性
- 队列中的数据始终保持最新
- 避免丢失重要的实时数据

### 2. 调试灵活性
- 可以根据需要开启/关闭调试信息
- 减少不必要的日志输出

### 3. 系统稳定性
- FIFO策略确保队列不会无限增长
- 避免内存溢出问题

### 4. 性能优化
- 减少不必要的日志输出
- 提高系统运行效率

## 🎯 总结

**FIFO队列和调试功能实现完成！**

- ✅ **FIFO丢弃策略**: 队列满时优先丢弃旧数据
- ✅ **CLI调试控制**: 新增`dropped`命令控制显示
- ✅ **配置集中化**: 调试配置在Config中统一管理
- ✅ **向后兼容**: 不影响现有功能

**现在系统会优先保持最新数据，并提供灵活的调试控制！**

---

**实现时间**: 2025年1月  
**版本**: V3.3.1  
**状态**: ✅ 已实现  
**测试**: 待验证
