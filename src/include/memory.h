#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include "common.h"
#include "object.h"

// 分配数组
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

// 释放对象空间
#define FREE(type, pointer)reallocate(pointer, sizeof(type), 0);

// 增大容量
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

// 释放动态空间
#define FREE_ARRAY(type, pointer, oldCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), 0)

// 增大动态空间
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

// 内存分配管理
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// 标记对象
void markObject(Obj* object);

// 标记值
void markValue(Value value);

// 垃圾回收器
void collectGarbage();

// 释放多个对象
void freeObjects();

#endif
