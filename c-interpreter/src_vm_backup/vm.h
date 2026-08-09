/* Lumi Bytecode Virtual Machine - VM Engine Header
 * ==========================================================
 */
#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lumi.h"
#include "chunk.h"

#define STACK_MAX 65536
#define FRAMES_MAX 2048
#define TRY_MAX 256

typedef struct {
    Chunk *chunk;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct {
    size_t handler_ip_offset;
    size_t stack_depth;
    int frame_index;
} VMTryHandler;

typedef struct {
    Chunk *chunk;
    uint8_t *ip;
    Value stack[STACK_MAX];
    Value *stack_top;
    CallFrame frames[FRAMES_MAX];
    int frame_count;
    VMTryHandler try_stack[TRY_MAX];
    int try_count;
    Interp *in;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void vm_init(VM *vm, Interp *in);
void vm_free(VM *vm);
InterpretResult run_vm(VM *vm, Chunk *chunk);

#endif /* VM_H */
