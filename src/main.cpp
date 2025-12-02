#include <Arduino.h>
#include "TaskManager.h"

// 全局任务管理器实例
TaskManager* taskManager = nullptr;

void setup() {
  // 初始化串口
  Serial0.begin(921600);
  delay(1000);
  
  Serial0.printf("\n");
  Serial0.printf("========================================\n");
  Serial0.printf("    ESP32-S3 传感器网关系统启动\n");
  Serial0.printf("    版本: V3.3\n");
  Serial0.printf("    编译时间: %s %s\n", __DATE__, __TIME__);
  Serial0.printf("========================================\n");
  Serial0.printf("\n");
  
  // 创建任务管理器
  taskManager = new TaskManager();
  if (!taskManager) {
    Serial0.printf("[MAIN] ERROR: Failed to create TaskManager\n");
    return;
  }
  
  // 初始化任务管理器
  if (!taskManager->initialize()) {
    Serial0.printf("[MAIN] ERROR: Failed to initialize TaskManager\n");
    delete taskManager;
    taskManager = nullptr;
    return;
  }
  
  // 启动所有任务
  if (!taskManager->startTasks()) {
    Serial0.printf("[MAIN] ERROR: Failed to start tasks\n");
    delete taskManager;
    taskManager = nullptr;
    return;
  }
  
  Serial0.printf("[MAIN] System initialized successfully\n");
  Serial0.printf("[MAIN] Type 'help' for available commands\n");
  Serial0.printf("\n");
}

void loop() {
  // 主循环只处理系统监控和错误恢复
  static uint32_t lastHealthCheck = 0;
  const uint32_t HEALTH_CHECK_INTERVAL = 60000; // 60秒
  
  uint32_t now = millis();
  
  // 定期健康检查
  if (now - lastHealthCheck >= HEALTH_CHECK_INTERVAL) {
    if (taskManager) {
      taskManager->getSystemStatus();
    }
    lastHealthCheck = now;
  }
  
  // 主循环延迟
  delay(1000);
}