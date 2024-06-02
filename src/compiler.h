#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "object.h"
#include "vm.h"

// 编译源代码字符串
ObjFunction* compile(const char* source);

// 标记编译期访问的值
void markCompilerRoots();

#endif