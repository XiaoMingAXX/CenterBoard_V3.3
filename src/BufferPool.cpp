#include "BufferPool.h"

BufferPool::BufferPool() {
    blockQueue = nullptr;
    mutex = nullptr;
    poolSize = 0;
    
    memset(&stats, 0, sizeof(stats));
    
    Serial0.printf("[BufferPool] Created\n");
}

BufferPool::~BufferPool() {
    if (blockQueue) {
        vQueueDelete(blockQueue);
    }
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    
    Serial0.printf("[BufferPool] Destroyed\n");
}

bool BufferPool::initialize(size_t poolSize) {
    this->poolSize = poolSize;
    
    // 创建互斥锁
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        Serial0.printf("[BufferPool] ERROR: Failed to create mutex\n");
        return false;
    }
    
    // 创建块队列
    blockQueue = xQueueCreate(poolSize, sizeof(DataBlock*));
    if (!blockQueue) {
        Serial0.printf("[BufferPool] ERROR: Failed to create block queue\n");
        return false;
    }
    
    // 预分配数据块
    preallocateBlocks();
    
    Serial0.printf("[BufferPool] Initialized with %d blocks\n", poolSize);
    return true;
}

DataBlock* BufferPool::acquireBlock() {
    DataBlock* block = nullptr;
    
    if (xQueueReceive(blockQueue, &block, 0) == pdTRUE) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            stats.totalAcquisitions++;
            stats.availableBlocks--;
            stats.usedBlocks++;
            xSemaphoreGive(mutex);
        }
        return block;
    }
    
    // 队列为空，尝试创建新块
    block = createBlock();
    if (block) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            stats.totalAcquisitions++;
            stats.usedBlocks++;
            xSemaphoreGive(mutex);
        }
    } else {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            stats.allocationFailures++;
            xSemaphoreGive(mutex);
        }
    }
    
    return block;
}

void BufferPool::releaseBlock(DataBlock* block) {
    if (!block) {
        return;
    }
    
    // 重置块数据
    memset(block, 0, sizeof(DataBlock));
    
    // 将块返回队列
    if (xQueueSend(blockQueue, &block, 0) == pdTRUE) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            stats.totalReleases++;
            stats.availableBlocks++;
            stats.usedBlocks--;
            xSemaphoreGive(mutex);
        }
    } else {
        // 队列满，释放内存
        free(block);
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            stats.totalReleases++;
            stats.usedBlocks--;
            xSemaphoreGive(mutex);
        }
    }
}

size_t BufferPool::getAvailableBlocks() const {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        size_t result = stats.availableBlocks;
        xSemaphoreGive(mutex);
        return result;
    }
    return 0;
}

size_t BufferPool::getTotalBlocks() const {
    return stats.totalBlocks;
}

BufferPool::Stats BufferPool::getStats() const {
    return stats;
}

void BufferPool::resetStats() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        memset(&stats, 0, sizeof(stats));
        stats.totalBlocks = poolSize;
        stats.availableBlocks = poolSize;
        xSemaphoreGive(mutex);
    }
}

void BufferPool::preallocateBlocks() {
    for (size_t i = 0; i < poolSize; i++) {
        DataBlock* block = createBlock();
        if (block) {
            if (xQueueSend(blockQueue, &block, 0) != pdTRUE) {
                free(block);
                Serial0.printf("[BufferPool] WARNING: Failed to add block %d to queue\n", i);
            }
        }
    }
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        stats.totalBlocks = poolSize;
        stats.availableBlocks = poolSize;
        xSemaphoreGive(mutex);
    }
    
    Serial0.printf("[BufferPool] Preallocated %d blocks\n", poolSize);
}

DataBlock* BufferPool::createBlock() {
    DataBlock* block = (DataBlock*)malloc(sizeof(DataBlock));
    if (block) {
        memset(block, 0, sizeof(DataBlock));
    }
    return block;
}
