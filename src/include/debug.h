// 反汇编调试

#ifndef CLOX_DEBUG_H
#define CLOX_DEBUG_H

#include "chunk.h"

// 反汇编指定数据块
void disassembleChunk(Chunk* chunk, const char* name);

// 反汇编指令
int disassembleInstruction(Chunk* chunk, int offset);

#endif