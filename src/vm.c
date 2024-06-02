#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "object.h"
#include "memory.h"

/*
 * 不再直接从VM中读取字节码块和ip
 * 转而从栈顶的CallFrame获取
*/

uint16_t seed = 0xACE1u;;// 随机数种子
VM vm;

static void runtimeError(const char* format, ...);

static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

// 伪随机数生成
static Value randomValue(int argCount, Value* args) {
    
    if (argCount == 1) {seed = (uint16_t)AS_NUMBER(*args);}
    if (argCount > 1) {printf("Value Error!\n");return NUMBER_VAL(-1);}
    uint16_t bit = ((seed >> 0) ^ (seed >> 2) ^ (seed >> 3) ^ (seed >> 5)) & 1;
    seed = (seed >> 1) | (bit << 15);
    return NUMBER_VAL(seed);
}

// 真随机数生成
static Value realRandomValue(){
    int ret;
    asm volatile("rdrand %0" : "=r"(ret));
    return NUMBER_VAL(ret & 0xfff);
}

// 根号分之一
static Value Qrsqrt(int argCount, Value* args) {
    if (AS_NUMBER(*args) > 0 && argCount == 1) {
        long i;
        const float threehalfs = 1.5F;
        float x = AS_NUMBER(*args) * 0.5F;
        float y = AS_NUMBER(*args);
        i = * (long *) &y;
        i = 0x5f3759df - (i >> 1);
        y = * (float *) &i;
        y = y * (threehalfs - (x * y * y)); 
        return NUMBER_VAL(y);
    }
    printf("Q_rsqrt need a greater zero number.\n");
    return NIL_VAL;
}

// 开根号
static Value mSqrt(int argCount, Value* args) {
    if (AS_NUMBER(*args) >= 0 && argCount == 1) {
        double x = AS_NUMBER(*args);
        double y = 1;
        if (x == 0){return NUMBER_VAL(0);}
        for (int i = 0; i < 8; i++) {
            y = 0.5F * (y + x/y);
        }
        return NUMBER_VAL(y);
    } else {
        printf("Value Error!\n");
        return NIL_VAL;
    }
}

// 退出函数
static Value Exit() {
    exit(0);
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
    int i = 0;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
            function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    // CallFrame* frame = &vm.frames[vm.frameCount - 1];
    // size_t instruction = frame->ip - frame->function->chunk.code - 1;
    // int line = frame->function->chunk.lines[instruction];
    // fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

// 定义标准库函数
static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings);
    vm.initString = NULL;
    vm.initString = copyString("init", 4);

    defineNative("clock", clockNative);
    defineNative("qsqrt", Qrsqrt);
    defineNative("sqrt", mSqrt);
    defineNative("rand", randomValue);
    defineNative("Rand", realRandomValue);
    defineNative("exit", Exit);

}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

// 抓取任意字段的数据
static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

// 调用函数
static bool call(ObjClosure* closure, int argCount) {
    // 检查形参与实参数量是否匹配
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", 
            closure->function->arity, argCount);
        return false;
    }
    // 检查栈溢出
    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow!");
        return false;
    }
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

// 调用不同类型的函数
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stackTop[-argCount - 1] = bound->receiver; // 接收器放置到局部槽0中
                return call(bound->method, argCount);
            }
            case OBJ_CLASS: {
                ObjClass* class = AS_CLASS(callee);
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(class));
                // 每创建一个新实例 自动调用init()
                Value initializer;
                if (tableGet(&class->methods, vm.initString,
                             &initializer)) {
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but get %d.", 
                                 argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            // case OBJ_FUNCTION: 
            //     return call(AS_FUNCTION(callee), argCount);
            case OBJ_NATIVE:{
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

// 获取方法闭包并将调用压入CallFrame
static bool invokeFromClass(ObjClass* class, ObjString* name, 
                            int argCount) {
    Value method;
    if (!tableGet(&class->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    return call(AS_CLOSURE(method), argCount);
}

// 从栈中抓取接收器 再转为实例对其调用方法
static bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);
    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }
    ObjInstance* instance = AS_INSTANCE(receiver);
    // 检查字段
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }
    return invokeFromClass(instance->class, name, argCount);
}

// 在类的方法表中查找方法
static bool bindMethod(ObjClass* class, ObjString* name) {
    Value method;
    if (!tableGet(&class->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    ObjBoundMethod* bound = newBoundMethod(peek(0),
                                           AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

// 捕获上值
static ObjUpvalue* captureUpvalue(Value* local) {
    // 寻找已存在的上值
    ObjUpvalue* preUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        preUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);

    // 插入新的上值
    createdUpvalue->next = upvalue;
    if (preUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        preUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

// 关闭上值
static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && 
           vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

// 类方法定义
static void defineMethod(ObjString* name) {
    Value method = peek(0);
    ObjClass* class = AS_CLASS(peek(1));
    tableSet(&class->methods, name, method);
    pop();
}

// 连接字符串
static void concatenate() {
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

// 判断语句是否为非(NIL和FALSE皆为非)
static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run(int flag) {

    CallFrame* frame = &vm.frames[vm.frameCount - 1];

    #define READ_BYTE() (*frame->ip++)
    
    #define READ_SHORT() \
        (frame->ip += 2, \
        (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

    #define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

    #define READ_STRING() AS_STRING(READ_CONSTANT())

    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                runtimeError("Operators must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
                } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } while (false);

    for (;;){

#ifdef DEBUG_TRACE_EXECUTION
    //printf("        ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        printf("[");
        printValue(*slot);
        printf("]");
    }
    printf("\n");
    // 从当前栈帧读取数据
    disassembleInstruction(&frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE())
        {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: {
                Value result = pop();
                if (flag==1){
                    printf("Ans = \n    ");
                    printValue(result);
                    printf("\n");
                }
                break;
                }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                // 赋值表达式的值需留在栈上不必弹出
                uint8_t slot = READ_BYTE();
                // 当前栈帧的槽
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_NOT: 
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE: {
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
            }
            push(NUMBER_VAL(-AS_NUMBER(pop())));
            //*(vm.stackTop-1) = -*(vm.stackTop-1);
            break;
            }
            // 定义并检查变量是否存在
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_GET_PROPERTY:{
                // 检查是否为实例
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(peek(0));// 从栈顶获取实例
                ObjString* name = READ_STRING();// 读取字段名

                // 查找条目
                Value value;
                // 查找是否为属性
                if (tableGet(&instance->fields, name, &value)) {
                    pop();// 实例
                    push(value);
                    break;
                }
                // 查找是否为方法
                if (!bindMethod(instance->class, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) {
                    runtimeError("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                // 将值绑定至实例
                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop(); // 实例
                push(value);
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError(
                        "Operators must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
                }

            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                // 调用成功 刷新frame
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));
                for(int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = 
                            captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE: {
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            }
            case OP_CLASS:
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }
                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
        }
    }
    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_CONSTANT
    #undef READ_STRING
    #undef BINARY_OP
}

InterpretResult interpret(const char* source, int flag)
{
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
    // callValue(OBJ_VAL(function), 0); // 执行第一个函数帧
    // CallFrame* frame = &vm.frames[vm.frameCount++];
    // frame->function = function;
    // frame->ip = function->chunk.code;
    // frame->slots = vm.stack;
    return run(flag);
    // Chunk chunk;
    // initChunk(&chunk);

    // if (!compile(source, &chunk)) {
    //     freeChunk(&chunk);
    //     return INTERPRET_COMPILE_ERROR;
    // }

    // vm.chunk = &chunk;
    // vm.ip = vm.chunk->code;

    // InterpretResult result = run();

    // freeChunk(&chunk);
    // // compile(source);
    // return result;
}

