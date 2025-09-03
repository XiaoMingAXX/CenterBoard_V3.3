# WebSocket服务器地址配置更新

## 🎯 更新内容

已成功修改WebSocket连接地址，现在使用正确的服务器路径：`/wxapp/esp32/batch_upload/`

## 🔧 修改的文件

### 1. `include/Config.h`
添加了WebSocket路径配置：
```cpp
static const char* WEBSOCKET_PATH;
```

### 2. `src/Config.cpp`
更新了网络配置：
```cpp
// 网络配置
const char* Config::WIFI_SSID = "xiaoming";
const char* Config::WIFI_PASSWORD = "LZMSDSG0704";
const char* Config::SERVER_URL = "175.178.100.179";  // 移除了http://前缀
const uint16_t Config::SERVER_PORT = 8000;
const char* Config::WEBSOCKET_PATH = "/wxapp/esp32/batch_upload/";  // 新增
```

### 3. `src/WebSocketClient.cpp`
- 添加了`#include "Config.h"`
- 修改WebSocket连接使用Config中的路径：
```cpp
webSocket.begin(serverUrl.c_str(), serverPort, Config::WEBSOCKET_PATH);
```

### 4. `src/TaskManager.cpp`
- 添加了`#include "Config.h"`
- 修改网络初始化使用Config中的参数：
```cpp
webSocketClient->initialize(Config::WIFI_SSID, Config::WIFI_PASSWORD, Config::SERVER_URL, Config::SERVER_PORT);
```

## 📋 配置说明

### 完整的WebSocket URL
现在系统会连接到：
```
ws://175.178.100.179:8000/wxapp/esp32/batch_upload/
```

### 配置参数
- **服务器地址**: `175.178.100.179`
- **端口**: `8000`
- **WebSocket路径**: `/wxapp/esp32/batch_upload/`

## 🔧 如何修改配置

### 修改服务器地址
在 `src/Config.cpp` 中修改：
```cpp
const char* Config::SERVER_URL = "你的服务器地址";
const uint16_t Config::SERVER_PORT = 你的端口;
```

### 修改WebSocket路径
在 `src/Config.cpp` 中修改：
```cpp
const char* Config::WEBSOCKET_PATH = "/你的/路径/";
```

### 修改WiFi配置
在 `src/Config.cpp` 中修改：
```cpp
const char* Config::WIFI_SSID = "你的WiFi名称";
const char* Config::WIFI_PASSWORD = "你的WiFi密码";
```

## 📊 编译结果

- **状态**: SUCCESS ✅
- **RAM使用率**: 14.1% (46,224 / 327,680 bytes)
- **Flash使用率**: 42.1% (882,157 / 1,310,720 bytes)

## 🎯 验证方法

### 1. 查看配置信息
使用CLI命令查看当前配置：
```
ESP32-S3> config
```

应该显示：
```
=== 系统配置 ===
固件版本: V3.3
设备编码: 2025001

网络配置:
  WiFi SSID: xiaoming
  服务器地址: 175.178.100.179:8000
  WebSocket路径: /wxapp/esp32/batch_upload/
```

### 2. 检查网络连接
使用CLI命令检查网络状态：
```
ESP32-S3> status
```

### 3. 观察连接日志
系统启动时应该看到：
```
[WebSocketClient] Connecting to WiFi: xiaoming
[WebSocketClient] WiFi connected. IP: 192.168.x.x
[WebSocketClient] Initialized. Server: 175.178.100.179:8000
```

## 🚀 总结

**WebSocket配置更新完成！**

- ✅ **服务器地址**: 已更新为正确的IP和端口
- ✅ **WebSocket路径**: 已设置为 `/wxapp/esp32/batch_upload/`
- ✅ **配置集中化**: 所有网络配置都在Config文件中
- ✅ **易于修改**: 只需修改Config.cpp即可更改所有网络参数

**现在系统会连接到正确的WebSocket服务器地址！**

---

**更新时间**: 2025年1月  
**版本**: V3.3.1  
**状态**: ✅ 已更新  
**测试**: 待验证
