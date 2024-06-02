#ifndef CLOX_TABLE_H
#define CLOX_TABLE_H

#include "common.h"
#include "value.h"

// 条目
typedef struct {
    ObjString* key;
    Value value;
} Entry;

// 哈希表
typedef struct {
    int count;// 键值对数
    int capacity;// 容量
    Entry* entries;
} Table;

// 初始化哈希表
void initTable(Table* table);

// 释放哈希表
void freeTable(Table* table);

// 检索哈希表
bool tableGet(Table* table, ObjString* key, Value* value);

// 插入哈希表
bool tableSet(Table* table, ObjString* key, Value value);

// 删除条目
bool tableDelete(Table* table, ObjString* key);

// 复制条目
void tableAddAll(Table* from, Table* to);

// 在所有已出现过的字符串中查找
ObjString* tableFindString(Table* table, const char* chars,
                           int length, uint32_t hash);

// 移除字符串表中不再使用的字符串
void tableRemoveWhite(Table* table);

// 标记全局变量哈希表
void markTable(Table* table);

#endif