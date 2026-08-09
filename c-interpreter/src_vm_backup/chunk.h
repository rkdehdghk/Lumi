/* Lumi Bytecode VM - Chunk Data Structure
 * ==========================================================
 */
#ifndef CHUNK_H
#define CHUNK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lumi.h"
#include "opcode.h"

typedef struct {
    Value *values;
    size_t count;
    size_t capacity;
} ValueArray;

typedef struct {
    uint8_t *code;
    int *lines;
    size_t count;
    size_t capacity;
    ValueArray constants;
} Chunk;

void value_array_init(ValueArray *array);
void value_array_write(ValueArray *array, Value value);
void value_array_free(ValueArray *array);

void chunk_init(Chunk *chunk);
void chunk_write(Chunk *chunk, uint8_t byte, int line);
size_t chunk_add_constant(Chunk *chunk, Value value);
void chunk_free(Chunk *chunk);

#endif /* CHUNK_H */
