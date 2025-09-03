#include "SensorData.h"
#include "Config.h"
#include "BufferPool.h"

SensorData::SensorData(BufferPool* bufferPoolInstance) {
    currentBlock = nullptr;
    blockQueue = xQueueCreate(10, sizeof(DataBlock*));
    mutex = xSemaphoreCreateMutex();
    
    // 使用传入的BufferPool实例，如果没有则创建新的
    if (bufferPoolInstance) {
        bufferPool = bufferPoolInstance;
        ownsBufferPool = false;
        Serial.printf("[SensorData] Using provided BufferPool instance\n");
    } else {
        bufferPool = new BufferPool();
        ownsBufferPool = true;
        if (bufferPool) {
            bufferPool->initialize(20);
        }
        Serial.printf("[SensorData] Created new BufferPool instance\n");
    }
    
    // 初始化统计信息
    memset(&stats, 0, sizeof(stats));
    lastStatsTime = millis();
    frameCountSinceLastStats = 0;
    
    Serial.printf("[SensorData] Initialized with block queue size: 10\n");
}

SensorData::~SensorData() {
    if (blockQueue) {
        vQueueDelete(blockQueue);
    }
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    if (currentBlock) {
        if (bufferPool) {
            bufferPool->releaseBlock(currentBlock);
        } else {
            free(currentBlock);
        }
    }
    if (bufferPool && ownsBufferPool) {
        delete bufferPool;
    }
}

bool SensorData::addFrame(const SensorFrame& frame) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        // 如果没有当前块或当前块已满，创建新块
        if (!currentBlock || currentBlock->isFull) {
            currentBlock = createNewBlock();
            if (!currentBlock) {
                xSemaphoreGive(mutex);
                stats.droppedFrames++;
                return false;
            }
        }
        
        // 添加帧到当前块
        currentBlock->frames[currentBlock->frameCount] = frame;
        currentBlock->frameCount++;
        
        // 检查块是否已满
        if (currentBlock->frameCount >= DataBlock::MAX_FRAMES) {
            currentBlock->isFull = true;
            currentBlock->createTime = millis();
            
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
                        if (bufferPool) {
                            bufferPool->releaseBlock(oldBlock);
                        } else {
                            free(oldBlock);
                        }
                    }
                }
                
                // 再次尝试添加新数据块
                if (xQueueSend(blockQueue, &currentBlock, 0) != pdTRUE) {
                    if (Config::SHOW_DROPPED_PACKETS) {
                        Serial.printf("[SensorData] ERROR: Failed to add block to queue after dropping old data\n");
                    }
                    if (bufferPool) {
                        bufferPool->releaseBlock(currentBlock);
                    } else {
                        free(currentBlock);
                    }
                    stats.droppedFrames += currentBlock->frameCount;
                    currentBlock = nullptr;
                } else {
                    stats.blocksCreated++;
                    stats.totalFrames += currentBlock->frameCount;
                    currentBlock = nullptr;
                }
            } else {
                stats.blocksCreated++;
                stats.totalFrames += currentBlock->frameCount;
                currentBlock = nullptr;
            }
        }
        
        frameCountSinceLastStats++;
        updateStats();
        
        xSemaphoreGive(mutex);
        return true;
    }
    return false;
}

DataBlock* SensorData::getNextBlock() {
    DataBlock* block = nullptr;
    if (xQueueReceive(blockQueue, &block, 0) == pdTRUE) {
        return block;
    }
    return nullptr;
}

void SensorData::releaseBlock(DataBlock* block) {
    if (block) {
        if (bufferPool) {
            // 使用BufferPool释放块
            bufferPool->releaseBlock(block);
        } else {
            // 回退到直接释放
            free(block);
        }
        stats.blocksSent++;
    }
}

SensorData::Stats SensorData::getStats() const {
    return stats;
}

void SensorData::resetStats() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        memset(&stats, 0, sizeof(stats));
        lastStatsTime = millis();
        frameCountSinceLastStats = 0;
        xSemaphoreGive(mutex);
    }
}

const char* SensorData::getSensorType(uint8_t sensorId) {
    switch (sensorId) {
        case 1: return "waist";
        case 2: return "shoulder";
        case 3: return "wrist";
        case 4: return "racket";
        default: return "unknown";
    }
}

void SensorData::updateStats() {
    uint32_t now = millis();
    if (now - lastStatsTime >= 1000) { // 每秒更新一次
        stats.avgFrameRate = (float)frameCountSinceLastStats * 1000.0f / (now - lastStatsTime);
        lastStatsTime = now;
        frameCountSinceLastStats = 0;
    }
}

DataBlock* SensorData::createNewBlock() {
    DataBlock* block = nullptr;
    
    if (bufferPool) {
        // 使用BufferPool获取块
        block = bufferPool->acquireBlock();
    } else {
        // 回退到直接分配
        block = (DataBlock*)malloc(sizeof(DataBlock));
    }
    
    if (block) {
        // 初始化块数据
        memset(block, 0, sizeof(DataBlock));
        block->blockId = stats.blocksCreated;
        block->createTime = millis();
    }
    
    return block;
}
