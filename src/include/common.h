// 定义通用数据结构

#ifndef CLOX_COMMON_H
#define CLOX_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 打印编译后的字节码指令
// #define DEBUG_PRINT_CODE

// 字节码运行时的堆栈状态
// #define DEBUG_TRACE_EXECUTION

// 内存回收压力测试
// #define DEBUG_STRESS_GC

// 回收日志
// #define DEBUG_LOG_GC

// 针对x86-64架构的值类型优化 
// #define NAN_BOXING

// 局部变量最大数量
#define UINT8_COUNT (UINT8_MAX + 1)

#define pi 3.141592653589793

#endif