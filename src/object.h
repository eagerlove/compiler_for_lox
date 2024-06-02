/* 定义对象类型 */
#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"

// 值类型
#define OBJ_TYPE(value)    (AS_OBJ(value)->type)

// 属性检查
#define IS_BOUND_METHOD    isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)    isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value)  isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)   isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)   isObjType(value, OBJ_STRING)

// 属性转换
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

// 对象类型
typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
} ObjType;

// 普通对象
struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj* next;
};

// 函数对象
typedef struct {
    Obj obj;
    int arity;// 函数参数数量
    int upvalueCount;// 上值数量
    Chunk chunk;
    ObjString* name;
} ObjFunction;

// 标准库函数引用(不解释为字节码，直接指向C代码)
typedef Value (*NativeFn)(int argCount, Value* args);

// 标准库函数结构体
typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

// 字符串结构体
struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

// 闭包上值结构体
typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

// 闭包函数结构体
typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;// 上值数组
    int upvalueCount;// 上值数量
} ObjClosure;

// 类结构体
typedef struct {
    Obj obj;
    ObjString* name;
    Table methods;
} ObjClass;

// 类实例
typedef struct {
    Obj* obj;
    ObjClass* class;
    Table fields;
} ObjInstance;

// 向实例绑定方法
typedef struct {
    Obj obj;
    Value receiver; // 接收器 this/self
    ObjClosure* method;
} ObjBoundMethod;

// 绑定方法
ObjBoundMethod* newBoundMethod(Value receiver,
                               ObjClosure* method);

// 类初始化
ObjClass* newClass(ObjString* name);

// 闭包函数创建并初始化
ObjClosure* newClosure(ObjFunction* function);

// 普通函数创建并初始化
ObjFunction* newFunction();

// 实例构造函数
ObjInstance* newInstance(ObjClass* class);

// 标准库构造函数
ObjNative* newNative(NativeFn function);

// 复制字符串(有所有权)
ObjString* takeString(char* chars, int length);

// 去除开头及结尾的引号并复制字符串(无所有权)
ObjString* copyString(const char* chars, int length);

// 上值构造函数
ObjUpvalue* newUpvalue(Value* slot);

// 打印对象
void printObject(Value value);

// 内联 检查值的类型
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}


#endif