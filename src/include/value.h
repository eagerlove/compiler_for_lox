#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include <string.h>
#include "common.h"

// 对象声明
typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

// 值类型 8字节
typedef uint64_t Value;

// 符号位
#define SIGN_BIT ((uint64_t)0x8000000000000000)

// double类型的所有指数位 + NaN + Intel value
#define QNAN     ((uint64_t)0x7ffc000000000000)

// 类型双关 将uint64_t值转为double值
static inline double valueToNum(Value value) {
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

// 类型双关 将double值转为uint64_t值
static inline Value numToValue(double num) {
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

// bool值
#define TAG_NIL   1 // 01
#define TAG_FALSE 2 // 10
#define TAG_TRUE  3 // 11

// 比对位信息检查类型标签
#define NIL_VAL         ((Value)(uint64_t)(QNAN | TAG_NIL))
#define FALSE_VAL       ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL        ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

#define IS_NIL(value)    ((value) == NIL_VAL)
#define IS_BOOL(value)   ((value | 1) == TRUE_VAL)
#define IS_NUMBER(value) ((value & QNAN) != QNAN)
#define IS_OBJ(value)    (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value)   ((value) == TRUE_VAL)
#define AS_NUMBER(value) valueToNum(value)
#define AS_OBJ(value) \
    ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#else

// 类型标签
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

// 值类型 16字节对齐
typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

// 检查值类型
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

// 转换值类型
#define AS_OBJ(value)     ((value).as.obj)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

// 定义值
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)    ((Value){VAL_OBJ, {.obj = (Obj*)object}})

#endif // NAN_BOXING

// 常量池
typedef struct 
{
    int capacity;
    int count;
    Value* values;
} ValueArray;

// 值相等
bool valuesEqual(Value a, Value b);

// 初始化常量空间
void initValueArray(ValueArray* array);

// 写入常量空间
void writeValueArray(ValueArray* array, Value value);

// 释放常量空间
void freeValueArray(ValueArray* array);

// 打印常量
void printValue(Value value);

#endif // CLOX_VALUE_H