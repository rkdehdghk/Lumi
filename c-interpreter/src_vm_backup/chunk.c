#include "chunk.h"
#include <stdlib.h>

void value_array_init(ValueArray *array) {
    array->values = NULL;
    array->count = 0;
    array->capacity = 0;
}

void value_array_write(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        size_t old_cap = array->capacity;
        array->capacity = old_cap < 8 ? 8 : old_cap * 2;
        array->values = (Value*)xrealloc(array->values, sizeof(Value) * array->capacity);
    }
    array->values[array->count] = retain(value);
    array->count++;
}

void value_array_free(ValueArray *array) {
    for (size_t i = 0; i < array->count; i++) {
        release(array->values[i]);
    }
    free(array->values);
    value_array_init(array);
}

void chunk_init(Chunk *chunk) {
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    value_array_init(&chunk->constants);
}

void chunk_write(Chunk *chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        size_t old_cap = chunk->capacity;
        chunk->capacity = old_cap < 8 ? 8 : old_cap * 2;
        chunk->code = (uint8_t*)xrealloc(chunk->code, sizeof(uint8_t) * chunk->capacity);
        chunk->lines = (int*)xrealloc(chunk->lines, sizeof(int) * chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

size_t chunk_add_constant(Chunk *chunk, Value value) {
    value_array_write(&chunk->constants, value);
    return chunk->constants.count - 1;
}

void chunk_free(Chunk *chunk) {
    free(chunk->code);
    free(chunk->lines);
    value_array_free(&chunk->constants);
    chunk_init(chunk);
}
