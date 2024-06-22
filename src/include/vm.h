#ifndef CLOX_VM_H
#define CLOX_VM_H

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

// 最大调用帧数
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

// 函数调用帧
typedef struct {
    ObjClosure* closure;// 闭包函数本体
    uint8_t* ip;// 调用者地址
    Value* slots;// 函数可供使用的槽
} CallFrame;

// 虚拟机结构体
typedef struct {
    CallFrame frames[FRAMES_MAX];// 栈帧
    int frameCount;// 函数帧
    // Chunk* chunk;
    // uint8_t* ip;// 字节指针，指向下一条将执行的指令
    Value stack[STACK_MAX];
    Value* stackTop; // 栈顶
    Table globals; // 常量表
    Table strings; // 哈希表结构
    ObjString* initString; // init函数名
    ObjUpvalue* openUpvalues;
    size_t bytesAllocated;
    size_t nextGC;
    Obj* objects;
    int grayCount;
    int grayCapacity;
    Obj** grayStack;

}VM;

// 返回值类型
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

// 初始化虚拟机
void initVM();

// 释放虚拟机资源
void freeVM();

// 解释运行并检查错误
InterpretResult interpret(const char* source, int flag);

// 压数据入栈
void push(Value value);

// 数据出栈
Value pop();

#endif