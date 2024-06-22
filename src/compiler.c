#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/common.h"
#include "include/compiler.h"
#include "include/scanner.h"
#include "include/memory.h"

#ifdef DEBUG_PRINT_CODE
#include "include/debug.h"
#endif

// 全局标识
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// 操作符优先级
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY

} Precedence;

// 函数类型
typedef void (*ParseFn)(bool canAssign);

// 虚函数->前，中缀运算符 
typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

// 局部变量
typedef struct {
    Token name; // 局部变量名
    int depth; // 局部变量作用域深度
    bool isCaptured; // 是否被捕获
} Local;

// 上值结构体
typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

// 函数类型
typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER, // 初始化器类型
    TYPE_METHOD,
    TYPE_SCRIPT
} FunctionType;

// 编译器状态
typedef struct Compiler{
    struct Compiler* enclosing;// 使用链表追踪嵌套函数
    ObjFunction* function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int localCount; // 局部变量个数
    Upvalue upvalues[UINT8_COUNT]; // 上值数组
    int scopeDepth;
} Compiler;

// 编译类结构体(提供最近邻外层的类信息)
typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
} ClassCompiler;

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL; // 不在任何类中
//Chunk* compilingChunk;

// 通过函数访问当前程序块
static Chunk* currentChunk() {
    return &current->function->chunk;
}

// 主体报错程序
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error ", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, "at end");
    } else if (token->type == TOKEN_ERROR) {

    } else {
        fprintf(stderr, "at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// 报告当前标识错误
static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// 报告前一个标识错误
static void error(const char* message) {
    errorAt(&parser.previous, message);
}

// 储存当前和下一个词法标识
static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

// 验证标识类型是否符合预期
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

// 检查类型
static bool check(TokenType type) {
    return parser.current.type == type;
}

// 匹配标识
static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// 写入单操作指令
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}


// 写入操作码与操作数
static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

// 循环指令
static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

// 写入跳转占位符
static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

// 退出块
static void emitReturn() {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0); // 初始化器返回时加载包含实例的槽0
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

// 确保常量数量不大于256
static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

// 处理常量
static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

// 计算真实偏移量并替换占位符
static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

// 初始化编译器
static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    current = compiler;
    // 堆存储函数名
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start,
                                             parser.previous.length);
    }
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    // 函数调用：存储调用函数
    // 方法调用：存储接收器
    if (type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

// 结束编译
static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
    #ifdef DEBUG_PRINT_CODE
        if (!parser.hadError) {
            disassembleChunk(currentChunk(), function->name != NULL
                ? function->name->chars : "<script>");
        }
    #endif
    current = current->enclosing;
    return function;
}

// 创建作用域
static void beginScope() {
    current->scopeDepth++;
}

// 结束作用域
static void endScope() {
    current->scopeDepth--;
    // 查找上一个作用域深度的局部变量并丢弃
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth >
                current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured){
            emitByte(OP_CLOSE_UPVALUE); // 追踪被闭包所捕获的变量，调整入堆的时机
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
                }
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void synchronize();
static void parsePrecedence(Precedence precedence);

// 将变量名存入常量表并返回索引
static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start,
                                           name->length)));
}

// 判断两个变量词素是否相同
static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// 向后检索局部变量 无则返回-1
static int resolveLocal(Compiler* compiler, Token* name) {
    int i = 0;
    for (i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer");
            }
            return i;
        }
    }
    return -1;
}

// 向上值数组添加被闭包函数引用的值
static int addUpvalue(Compiler* compiler, uint8_t index,
                      bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

// 查找闭包引用值
static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1){
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }
    return -1;
}

// 存储局部变量及该变量的作用域深度
static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;// 未定义
    local->isCaptured = false;
}

// 记录局部变量并检查变量是否重叠
static void declareVariable() {
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;
    int i = 0;
    for (i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }
    addLocal(*name);
}

// 分析变量
static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);
    declareVariable();
    if (current->scopeDepth > 0) return 0;
    return identifierConstant(&parser.previous);
}

// 标记变量已被初始化
static void markInitialized() {
    if (current->scopeDepth == 0) return;// 判断是否处于局部作用域
    current->locals[current->localCount - 1].depth =
        current->scopeDepth;
}

// 定义变量
static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

// 参数列表
static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
            } while (match(TOKEN_COMMA));
        }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

// and: 与功能
static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

// 加减乘除中缀操作符解析
static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        default: return;
    }
}

// 函数调用
static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

// .访问实例的属性
static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);

    // 优先级判断避免混淆 (a+b.c=3)
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }
}

// 布尔值解析
static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL:   emitByte(OP_NIL); break;
        case TOKEN_TRUE:  emitByte(OP_TRUE); break;
        default: return;
    }
}

// 括号识别
static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// 处理数字
static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

// or: 或运算
static void or_(bool canAssign) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

// 处理字符 去除两端引号
static void string(bool canAssign) {
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                    parser.previous.length - 2)));
}

// 加载变量
static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1){
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

// 读取变量
static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

// this 以局部变量形式存在
static void this_(bool canAssign) {
    if (current == NULL) {
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}

// 前缀运算符处理
static void unary(bool canAssign) {
    TokenType operateType = parser.previous.type;

    parsePrecedence(PREC_UNARY);
    switch (operateType) {
        case TOKEN_BANG:  emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE);break;
        default: return;
    }
}

// 优先级解析表
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]       = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]              = {NULL,     dot,    PREC_CALL},
    [TOKEN_MINUS]            = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]             = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]            = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]             = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]             = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]       = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]      = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]             = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]       = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]           = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]           = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]              = {NULL,     and_,   PREC_AND},
    [TOKEN_CLASS]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]             = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]            = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]              = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]              = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]               = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]              = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]               = {NULL,     or_,    PREC_OR},
    [TOKEN_PRINT]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]             = {this_,    NULL,   PREC_NONE},
    [TOKEN_TRUE]             = {literal,  NULL,   PREC_NONE},
    [TOKEN_VAR]              = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]              = {NULL,     NULL,   PREC_NONE},

};

// 解析优先级
static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }
    // 根据优先级决定是否可以赋值
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

// 查询优先级
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

// 表达式处理
static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

// 执行代码块
static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

// 函数编译
static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    // 函数参数处理
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                error("Cannot have more than 255 parameters.");
            }
            uint8_t constant = parseVariable(
                "Can't have more than 255 parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

// 类方法解析
static void method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifierConstant(&parser.previous);
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 && 
        memcmp(parser.previous.start, "init", 4) == 0) {
      type = TYPE_INITIALIZER;
    }
    function(type);
    emitBytes(OP_METHOD, constant);
}

// class标识：声明类
static void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);// 将类名将类对象与常量绑定

    // 类声明
    declareVariable();
    emitBytes(OP_CLASS, nameConstant);
    defineVariable(nameConstant);
    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    // 继承 与类名使用 < 连接
    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);
        // 错误处理：继承自己
        if (identifiersEqual(&className, &parser.previous)) {
            error("A class can't inherit from itself.");
        }
        namedVariable(className, false);
        emitByte(OP_INHERIT);
    }
    namedVariable(className, false);

    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        method();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OP_POP);
    currentClass = currentClass->enclosing;
}

// fun标识：声明函数
static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized(); // 在编译函数主体之前就将函数声明的变量标记为已初始化
    function(TYPE_FUNCTION);
    defineVariable(global);
}

// var标识: 声明变量
static void varDeclaration() {
    uint8_t global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON,
            "Expect ';' after variable declaration.");

    defineVariable(global);
}

// 表达式
static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

// for语句
static void forStatement() {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // 解析for的三个子语句
    if (match(TOKEN_SEMICOLON)) {
        // 跳过分号
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        expressionStatement();
    }

    // 记录偏移量以便跳转
    int loopStart = currentChunk()->count;
    // 条件子句
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // 条件为非 跳出循环
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        // 跳过增量字句进入循环体
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        //每次迭代结束后跳转到增量表达式
        emitLoop(loopStart);
        loopStart = incrementStart;
        
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);
    // 保持堆栈高度
    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }
    endScope();
}

// if语句
static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    // 占位符 
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);
    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

// 解析声明
static void declaration() {
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    }else if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }
    if (parser.panicMode) synchronize();
}

// 输出语句
static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

// 返回语句
static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        // 初始化器不返回任何值
        if (current->type == TYPE_INITIALIZER) {
            error("Can't return a value from an initializer.");
        }
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

// while语句
static void whileStatement() {
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after codition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    emitLoop(loopStart);
    patchJump(exitJump);
    emitByte(OP_POP);

}

// 错误同步
static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                ;
        }
        advance();
    }
}

// 语句处理
static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    }else if (match(TOKEN_WHILE)){
        whileStatement();
    }else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);
    parser.hadError = false;
    parser.panicMode = false;
    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }
    /*expression();
    consume(TOKEN_EOF, "Expect end of expression.");*/
    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
    /* int line = -1;
    for (;;) {
        Token token = scanToken();
        if (token.line != line) {
            printf("%4d ", token.line);
            line = token.line;
        } else {
            printf("    | ");
        }
        printf("%2d  '%.*s'\n", token.type, token.length, token.start);
        if (token.type == TOKEN_EOF) break;
    } */
}

void markCompilerRoots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}