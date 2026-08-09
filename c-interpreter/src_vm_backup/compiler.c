#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void compile_node(Node *node, Chunk *chunk);

static void emit_byte(Chunk *chunk, uint8_t byte, int line) {
    chunk_write(chunk, byte, line);
}

static void emit_bytes(Chunk *chunk, uint8_t byte1, uint8_t byte2, int line) {
    emit_byte(chunk, byte1, line);
    emit_byte(chunk, byte2, line);
}

static size_t emit_jump(Chunk *chunk, uint8_t instruction, int line) {
    emit_byte(chunk, instruction, line);
    emit_byte(chunk, 0xff, line);
    emit_byte(chunk, 0xff, line);
    return chunk->count - 2;
}

static void patch_jump(Chunk *chunk, size_t offset) {
    size_t jump = chunk->count - offset - 2;
    if (jump > 65535) {
        fprintf(stderr, "Jump offset too large in bytecode compilation.\n");
    }
    chunk->code[offset] = (jump >> 8) & 0xff;
    chunk->code[offset + 1] = jump & 0xff;
}

static void emit_loop(Chunk *chunk, size_t loop_start, int line) {
    emit_byte(chunk, OP_LOOP, line);
    size_t jump = chunk->count - loop_start + 2;
    if (jump > 65535) {
        fprintf(stderr, "Loop body too large.\n");
    }
    emit_byte(chunk, (jump >> 8) & 0xff, line);
    emit_byte(chunk, jump & 0xff, line);
}

static uint8_t add_string_constant(Chunk *chunk, const char *str) {
    Value s = str_value(str);
    size_t idx = chunk_add_constant(chunk, s);
    release(s);
    return (uint8_t)idx;
}

bool is_expression_node(const Node *node) {
    if (!node) return false;
    switch (node->kind) {
        case N_NUMBER: case N_STRING: case N_FSTRING: case N_BOOL: case N_NONE:
        case N_VAR: case N_BINOP: case N_UNARY: case N_MEMBER:
        case N_METHODCALL: case N_THIS: case N_SUPER: case N_LIST: case N_TUPLE:
        case N_DICT: case N_SLICE: case N_INDEX: case N_RANGESPEC:
        case N_LIST_COMP: case N_DICT_COMP: case N_FORR:
            return true;
        case N_FUNCDEF:
            return node->v.funcdef->name == NULL || strcmp(node->v.funcdef->name, "nameless") == 0;
        case N_CALL:
            return strcmp(node->v.call.name, "print") != 0;
        default:
            return false;
    }
}

static void compile_statement(Node *node, Chunk *chunk) {
    if (!node) return;
    compile_node(node, chunk);
    if (is_expression_node(node)) {
        emit_byte(chunk, OP_POP, node->line);
    }
}


static void compile_nodelist(NodeList *list, Chunk *chunk) {
    if (!list) return;
    for (size_t i = 0; i < list->len; i++) {
        compile_statement(list->items[i], chunk);
    }
}

static void compile_node(Node *node, Chunk *chunk) {
    if (!node) return;
    int line = node->line;

    switch (node->kind) {
        case N_NUMBER: {
            Value v = node->v.num.is_float ? float_val(node->v.num.f) : int_val(node->v.num.i);
            uint8_t idx = (uint8_t)chunk_add_constant(chunk, v);
            emit_bytes(chunk, OP_CONSTANT, idx, line);
            break;
        }
        case N_STRING: {
            Value v = obj_val(node->v.str.value);
            /* retain because node owns value, retain for constant table */
            uint8_t idx = (uint8_t)chunk_add_constant(chunk, v);
            emit_bytes(chunk, OP_CONSTANT, idx, line);
            break;
        }
        case N_BOOL: {
            emit_byte(chunk, node->v.bool_.value ? OP_TRUE : OP_FALSE, line);
            break;
        }
        case N_NONE: {
            emit_byte(chunk, OP_NIL, line);
            break;
        }
        case N_VAR: {
            uint8_t name_idx = add_string_constant(chunk, node->v.var.name);
            emit_bytes(chunk, OP_GET_GLOBAL, name_idx, line);
            break;
        }
        case N_ASSIGN: {
            /* typed declaration (e.g. "bytes empty") needs interpreter for
               default_for_type / coerce_to_type, so fall through to OP_EVAL_AST */
            if (node->v.assign.type_name) goto eval_ast;
            if (node->v.assign.value) {
                compile_node(node->v.assign.value, chunk);
            } else {
                emit_byte(chunk, OP_NIL, line);
            }
            uint8_t name_idx = add_string_constant(chunk, node->v.assign.name);
            emit_bytes(chunk, OP_SET_GLOBAL, name_idx, line);
            break;
        }
        case N_BINOP: {
            compile_node(node->v.binop.left, chunk);
            compile_node(node->v.binop.right, chunk);
            const char *op = node->v.binop.op;
            if (strcmp(op, "+") == 0) emit_byte(chunk, OP_ADD, line);
            else if (strcmp(op, "-") == 0) emit_byte(chunk, OP_SUB, line);
            else if (strcmp(op, "*") == 0) emit_byte(chunk, OP_MUL, line);
            else if (strcmp(op, "/") == 0) emit_byte(chunk, OP_DIV, line);
            else if (strcmp(op, "%") == 0) emit_byte(chunk, OP_MOD, line);
            else if (strcmp(op, "**") == 0 || strcmp(op, "^") == 0) emit_byte(chunk, OP_POW, line);
            else if (strcmp(op, "==") == 0) emit_byte(chunk, OP_EQUAL, line);
            else if (strcmp(op, "!=") == 0) { emit_byte(chunk, OP_EQUAL, line); emit_byte(chunk, OP_NOT, line); }
            else if (strcmp(op, ">") == 0) emit_byte(chunk, OP_GREATER, line);
            else if (strcmp(op, "<") == 0) emit_byte(chunk, OP_LESS, line);
            else if (strcmp(op, ">=") == 0) { emit_byte(chunk, OP_LESS, line); emit_byte(chunk, OP_NOT, line); }
            else if (strcmp(op, "<=") == 0) { emit_byte(chunk, OP_GREATER, line); emit_byte(chunk, OP_NOT, line); }
            break;
        }
        case N_UNARY: {
            compile_node(node->v.unary.operand, chunk);
            if (strcmp(node->v.unary.op, "-") == 0) emit_byte(chunk, OP_NEG, line);
            else if (strcmp(node->v.unary.op, "not") == 0 || strcmp(node->v.unary.op, "!") == 0) emit_byte(chunk, OP_NOT, line);
            break;
        }
        case N_MULTI: {
            compile_nodelist(&node->v.multi.body, chunk);
            break;
        }
        case N_IF: {
            compile_node(node->v.if_.cond, chunk);
            size_t else_jump = emit_jump(chunk, OP_JUMP_IF_FALSE, line);

            compile_nodelist(&node->v.if_.then_body, chunk);
            size_t end_jump = emit_jump(chunk, OP_JUMP, line);

            patch_jump(chunk, else_jump);

            if (node->v.if_.has_else) {
                compile_nodelist(&node->v.if_.else_body, chunk);
            }
            patch_jump(chunk, end_jump);
            break;
        }
        case N_WHILE: {
            size_t loop_start = chunk->count;
            compile_node(node->v.while_.cond, chunk);
            size_t exit_jump = emit_jump(chunk, OP_JUMP_IF_FALSE, line);

            compile_nodelist(&node->v.while_.body, chunk);
            emit_loop(chunk, loop_start, line);

            patch_jump(chunk, exit_jump);
            break;
        }
        case N_FUNCDEF: {
            Value node_val;
            node_val.kind = V_NONE;
            node_val.as.o = (Obj*)node;
            uint8_t idx = (uint8_t)chunk_add_constant(chunk, node_val);
            emit_bytes(chunk, OP_CLOSURE, idx, line);
            if (node->v.funcdef->name && strcmp(node->v.funcdef->name, "nameless") != 0) {
                uint8_t name_idx = add_string_constant(chunk, node->v.funcdef->name);
                emit_bytes(chunk, OP_SET_GLOBAL, name_idx, line);
            }
            break;
        }
        case N_CLASSDEF: {
            Value node_val;
            node_val.kind = V_NONE;
            node_val.as.o = (Obj*)node;
            uint8_t idx = (uint8_t)chunk_add_constant(chunk, node_val);
            emit_bytes(chunk, OP_CLASS_DEF, idx, line);
            break;
        }
        case N_MEMBER: {
            compile_node(node->v.member.obj, chunk);
            uint8_t name_idx = add_string_constant(chunk, node->v.member.name);
            emit_bytes(chunk, OP_GET_PROPERTY, name_idx, line);
            break;
        }
        case N_MEMBERASSIGN: {
            compile_node(node->v.memberassign.obj, chunk);
            compile_node(node->v.memberassign.value, chunk);
            uint8_t name_idx = add_string_constant(chunk, node->v.memberassign.name);
            emit_bytes(chunk, OP_SET_PROPERTY, name_idx, line);
            break;
        }
        case N_METHODCALL: {
            compile_node(node->v.methodcall.obj, chunk);
            for (size_t i = 0; i < node->v.methodcall.args.len; i++) {
                compile_node(node->v.methodcall.args.items[i], chunk);
            }
            uint8_t name_idx = add_string_constant(chunk, node->v.methodcall.name);
            emit_byte(chunk, OP_INVOKE_METHOD, line);
            emit_byte(chunk, name_idx, line);
            emit_byte(chunk, (uint8_t)node->v.methodcall.args.len, line);
            break;
        }
        case N_THIS: {
            uint8_t name_idx = add_string_constant(chunk, "this");
            emit_bytes(chunk, OP_GET_GLOBAL, name_idx, line);
            break;
        }
        case N_SUPER: {
            uint8_t name_idx = add_string_constant(chunk, "super");
            emit_bytes(chunk, OP_GET_SUPER, name_idx, line);
            break;
        }
        case N_FORIN: {
            compile_node(node->v.forin.iterable, chunk);
            emit_byte(chunk, OP_GET_ITER, line);

            size_t loop_start = chunk->count;
            size_t exit_jump = emit_jump(chunk, OP_FOR_ITER, line);

            uint8_t var_idx = add_string_constant(chunk, node->v.forin.var_name);
            emit_bytes(chunk, OP_SET_GLOBAL, var_idx, line);

            compile_nodelist(&node->v.forin.body, chunk);
            emit_loop(chunk, loop_start, line);

            patch_jump(chunk, exit_jump);
            break;
        }
        case N_TRY: {
            size_t try_jump = emit_jump(chunk, OP_PUSH_TRY, line);
            compile_nodelist(&node->v.try_.try_body, chunk);
            emit_byte(chunk, OP_POP_TRY, line);
            size_t end_jump = emit_jump(chunk, OP_JUMP, line);

            patch_jump(chunk, try_jump);

            if (node->v.try_.ncatches > 0) {
                struct CatchClause *cc = &node->v.try_.catches[0];
                if (cc->var_name) {
                    uint8_t err_var = add_string_constant(chunk, cc->var_name);
                    emit_bytes(chunk, OP_SET_GLOBAL, err_var, line);
                } else {
                    emit_byte(chunk, OP_POP, line);
                }
                compile_nodelist(&cc->body, chunk);
            }
            patch_jump(chunk, end_jump);

            if (node->v.try_.has_always) {
                compile_nodelist(&node->v.try_.always_body, chunk);
            }
            break;
        }
        case N_RETURN: {
            if (node->v.ret.value) {
                compile_node(node->v.ret.value, chunk);
                emit_byte(chunk, OP_RETURN_VALUE, line);
            } else {
                emit_byte(chunk, OP_NIL, line);
                emit_byte(chunk, OP_RETURN_VALUE, line);
            }
            break;
        }
        case N_CALL: {
            bool has_keep = false;
            if (strcmp(node->v.call.name, "print") == 0) {
                size_t nargs = node->v.call.args.len;
                if (nargs > 0) {
                    Node *last_arg = node->v.call.args.items[nargs - 1];
                    if (last_arg && last_arg->kind == N_CALL && strcmp(last_arg->v.call.name, "keep") == 0) {
                        has_keep = true;
                    }
                }
                if (!has_keep) {
                    for (size_t i = 0; i < node->v.call.args.len; i++) {
                        compile_node(node->v.call.args.items[i], chunk);
                    }
                    emit_bytes(chunk, OP_PRINT, (uint8_t)node->v.call.args.len, line);
                    break;
                }
            }
            Value node_val;
            node_val.kind = V_NONE;
            node_val.as.o = (Obj*)node;
            uint8_t idx = (uint8_t)chunk_add_constant(chunk, node_val);
            emit_bytes(chunk, OP_EVAL_AST, idx, line);
            break;
        }
        case N_LIST: {
            for (size_t i = 0; i < node->v.list.elements.len; i++) {
                compile_node(node->v.list.elements.items[i], chunk);
            }
            emit_bytes(chunk, OP_BUILD_LIST, (uint8_t)node->v.list.elements.len, line);
            break;
        }
        case N_DICT: {
            for (size_t i = 0; i < node->v.dict.npairs; i++) {
                compile_node(node->v.dict.pairs[i].key, chunk);
                compile_node(node->v.dict.pairs[i].val, chunk);
            }
            emit_bytes(chunk, OP_BUILD_DICT, (uint8_t)node->v.dict.npairs, line);
            break;
        }
        case N_INDEX: {
            compile_node(node->v.index.target, chunk);
            compile_node(node->v.index.index, chunk);
            emit_byte(chunk, OP_GET_INDEX, line);
            break;
        }
        case N_INDEXASSIGN: {
            compile_node(node->v.indexassign.target, chunk);
            compile_node(node->v.indexassign.index, chunk);
            compile_node(node->v.indexassign.value, chunk);
            emit_byte(chunk, OP_SET_INDEX, line);
            break;
        }
        case N_SLICE: {
            compile_node(node->v.slice.target, chunk);
            if (node->v.slice.start) compile_node(node->v.slice.start, chunk);
            else emit_byte(chunk, OP_NIL, line);
            if (node->v.slice.end) compile_node(node->v.slice.end, chunk);
            else emit_byte(chunk, OP_NIL, line);
            emit_byte(chunk, OP_SLICE, line);
            break;
        }
        eval_ast:
        default: {
            Value node_val;
            node_val.kind = V_NONE;
            node_val.as.o = (Obj*)node;
            uint8_t idx = (uint8_t)chunk_add_constant(chunk, node_val);
            emit_bytes(chunk, OP_EVAL_AST, idx, line);
            break;
        }
    }
}

bool compile_ast_to_chunk(NodeList *program, Chunk *chunk) {
    if (!program) return false;
    for (size_t i = 0; i < program->len; i++) {
        compile_statement(program->items[i], chunk);
    }
    emit_byte(chunk, OP_HALT, 0);
    return true;
}
