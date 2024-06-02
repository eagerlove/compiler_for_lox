#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include "common.h"

// 对象声明
typedef struct Obj Obj;
typedef struct ObjString ObjString;

// 动态类型种类
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

// 动态值类型
typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;


// 检查类型
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

// 读值
#define AS_OBJ(value)     ((value).as.obj)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

// 写值
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)    ((Value){VAL_OBJ, {.obj = (Obj*)object}})


//typedef double Value;

// 常量池
typedef struct 
{
    int capacity;
    int count;
    Value* values;
}ValueArray;

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


#endif