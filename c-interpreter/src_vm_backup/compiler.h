/* Lumi Bytecode VM - Compiler Header
 * ==========================================================
 */
#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lumi.h"
#include "chunk.h"

bool compile_ast_to_chunk(NodeList *program, Chunk *chunk);
bool is_expression_node(const Node *node);

#endif /* COMPILER_H */

