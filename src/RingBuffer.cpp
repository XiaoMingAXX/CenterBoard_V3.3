#include "RingBuffer.h"

RingBuffer::RingBuffer(size_t bufferSize) : size(bufferSize) {
    buffer = (uint8_t*)malloc(size);
    if (!buffer) {
        Serial0.printf("[RingBuffer] ERROR: Failed to allocate %d bytes\n", size);
        size = 0;
    }
    
    writePos = 0;
    readPos = 0;
    dataLength = 0;
    mutex = xSemaphoreCreateMutex();
    
    memset(&stats, 0, sizeof(stats));
    
    Serial0.printf("[RingBuffer] Initialized with size: %d bytes\n", size);
}

RingBuffer::~RingBuffer() {
    if (buffer) {
        free(buffer);
    }
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
}

bool RingBuffer::write(const uint8_t* data, size_t length) {
    if (!buffer || !data || length == 0) {
        return false;
    }
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        size_t writeSpace = getWriteSpace();
        if (length > writeSpace) {
            // 缓冲区溢出，丢弃旧数据
            stats.overflows++;
            size_t overflowBytes = length - writeSpace;
            readPos = (readPos + overflowBytes) % size;
            dataLength = size;
            Serial0.printf("[RingBuffer] WARNING: Buffer overflow, dropped %d bytes\n", overflowBytes);
        }
        
        // 写入数据
        for (size_t i = 0; i < length; i++) {
            buffer[writePos] = data[i];
            writePos = (writePos + 1) % size;
        }
        
        dataLength = getReadData();
        stats.totalWrites++;
        
        xSemaphoreGive(mutex);
        return true;
    }
    return false;
}

size_t RingBuffer::read(uint8_t* buffer, size_t maxLength) {
    if (!this->buffer || !buffer || maxLength == 0) {
        return 0;
    }
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        size_t readData = getReadData();
        size_t toRead = (readData < maxLength) ? readData : maxLength;
        
        for (size_t i = 0; i < toRead; i++) {
            buffer[i] = this->buffer[readPos];
            readPos = (readPos + 1) % size;
        }
        
        dataLength = getReadData();
        stats.totalReads++;
        
        if (toRead == 0 && maxLength > 0) {
            stats.underflows++;
        }
        
        xSemaphoreGive(mutex);
        return toRead;
    }
    return 0;
}

size_t RingBuffer::available() const {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        size_t result = dataLength;
        xSemaphoreGive(mutex);
        return result;
    }
    return 0;
}

size_t RingBuffer::freeSpace() const {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        size_t result = size - dataLength;
        xSemaphoreGive(mutex);
        return result;
    }
    return 0;
}

void RingBuffer::clear() {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        writePos = 0;
        readPos = 0;
        dataLength = 0;
        xSemaphoreGive(mutex);
    }
}

RingBuffer::Stats RingBuffer::getStats() const {
    return stats;
}

size_t RingBuffer::getWriteSpace() const {
    return size - dataLength;
}

size_t RingBuffer::getReadData() const {
    if (writePos >= readPos) {
        return writePos - readPos;
    } else {
        return size - readPos + writePos;
    }
}
