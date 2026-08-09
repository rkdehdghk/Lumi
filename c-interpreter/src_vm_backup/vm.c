#include "vm.h"
#include "compiler.h"
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reset_stack(VM *vm) {
    vm->stack_top = vm->stack;
}

void vm_init(VM *vm, Interp *in) {
    vm->in = in;
    vm->chunk = NULL;
    vm->ip = NULL;
    reset_stack(vm);
}

void vm_free(VM *vm) {
    /* Release any remaining stack elements */
    while (vm->stack_top > vm->stack) {
        vm->stack_top--;
        release(*vm->stack_top);
    }
}

static void push(VM *vm, Value value) {
    if (vm->stack_top - vm->stack >= STACK_MAX) {
        lumi_error(NO_LINE, "VM Stack overflow.");
    }
    *vm->stack_top = value;
    vm->stack_top++;
}

static Value pop(VM *vm) {
    if (vm->stack_top == vm->stack) {
        int line = (vm->chunk && vm->ip > vm->chunk->code) ? vm->chunk->lines[vm->ip - vm->chunk->code - 1] : 0;
        lumi_error(line, "VM Stack underflow at IP offset %d.", (int)(vm->ip - vm->chunk->code));
    }
    vm->stack_top--;
    return *vm->stack_top;
}

static Value peek(VM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

static bool is_falsey(Value value) {
    return !value_truthy(value);
}

InterpretResult run_vm(VM *vm, Chunk *chunk) {
    vm->chunk = chunk;
    vm->ip = chunk->code;

#define READ_BYTE() (*vm->ip++)
#define READ_SHORT() (vm->ip += 2, (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1]))
#define READ_CONSTANT() (chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STR(READ_CONSTANT())

    for (;;) {
        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_HALT:
            case OP_RETURN: {
                return INTERPRET_OK;
            }
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(vm, retain(constant));
                break;
            }
            case OP_NIL: push(vm, NONE_VAL); break;
            case OP_TRUE: push(vm, bool_val(true)); break;
            case OP_FALSE: push(vm, bool_val(false)); break;
            case OP_POP: {
                Value v = pop(vm);
                release(v);
                break;
            }
            case OP_GET_GLOBAL: {
                Str *name_str = READ_STRING();
                char *name = str_to_utf8(name_str);
                Value *val_ptr = env_lookup(vm->in->globals, name, NULL);
                if (!val_ptr) {
                    lumi_error(NO_LINE, "Undefined variable '%s'", name);
                }
                push(vm, retain(*val_ptr));
                free(name);
                break;
            }
            case OP_SET_GLOBAL: {
                Str *name_str = READ_STRING();
                char *name = str_to_utf8(name_str);
                Value val = pop(vm);
                env_declare(vm->in->globals, name, retain(val), NULL);
                release(val);
                free(name);
                break;
            }
            case OP_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                bool eq = key_equal(a, b);
                release(a);
                release(b);
                push(vm, bool_val(eq));
                break;
            }
            case OP_GREATER: {
                Value b = pop(vm);
                Value a = pop(vm);
                bool res = false;
                if (a.kind == V_INT && b.kind == V_INT) res = a.as.i > b.as.i;
                else if (IS_NUM(a) && IS_NUM(b)) {
                    double da = a.kind == V_INT ? (double)a.as.i : a.as.f;
                    double db = b.kind == V_INT ? (double)b.as.i : b.as.f;
                    res = da > db;
                }
                release(a); release(b);
                push(vm, bool_val(res));
                break;
            }
            case OP_LESS: {
                Value b = pop(vm);
                Value a = pop(vm);
                bool res = false;
                if (a.kind == V_INT && b.kind == V_INT) res = a.as.i < b.as.i;
                else if (IS_NUM(a) && IS_NUM(b)) {
                    double da = a.kind == V_INT ? (double)a.as.i : a.as.f;
                    double db = b.kind == V_INT ? (double)b.as.i : b.as.f;
                    res = da < db;
                }
                release(a); release(b);
                push(vm, bool_val(res));
                break;
            }
            case OP_ADD: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (a.kind == V_INT && b.kind == V_INT) {
                    push(vm, int_val(a.as.i + b.as.i));
                } else if (IS_NUM(a) && IS_NUM(b)) {
                    double da = a.kind == V_INT ? (double)a.as.i : a.as.f;
                    double db = b.kind == V_INT ? (double)b.as.i : b.as.f;
                    push(vm, float_val(da + db));
                } else if (a.kind == V_STR && b.kind == V_STR) {
                    Str *s = str_concat(AS_STR(a), AS_STR(b));
                    push(vm, obj_val(s));
                } else {
                    Value res = eval_binop_values(vm->in, "+", a, b, 0);
                    push(vm, res);
                }
                release(a); release(b);
                break;
            }
            case OP_SUB: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (a.kind == V_INT && b.kind == V_INT) {
                    push(vm, int_val(a.as.i - b.as.i));
                } else if (IS_NUM(a) && IS_NUM(b)) {
                    double da = a.kind == V_INT ? (double)a.as.i : a.as.f;
                    double db = b.kind == V_INT ? (double)b.as.i : b.as.f;
                    push(vm, float_val(da - db));
                } else {
                    Value res = eval_binop_values(vm->in, "-", a, b, 0);
                    push(vm, res);
                }
                release(a); release(b);
                break;
            }
            case OP_MUL: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (a.kind == V_INT && b.kind == V_INT) {
                    push(vm, int_val(a.as.i * b.as.i));
                } else if (IS_NUM(a) && IS_NUM(b)) {
                    double da = a.kind == V_INT ? (double)a.as.i : a.as.f;
                    double db = b.kind == V_INT ? (double)b.as.i : b.as.f;
                    push(vm, float_val(da * db));
                } else {
                    Value res = eval_binop_values(vm->in, "*", a, b, 0);
                    push(vm, res);
                }
                release(a); release(b);
                break;
            }
            case OP_DIV: {
                Value b = pop(vm);
                Value a = pop(vm);
                double da = a.kind == V_INT ? (double)a.as.i : a.as.f;
                double db = b.kind == V_INT ? (double)b.as.i : b.as.f;
                if (db == 0) lumi_error(NO_LINE, "Division by zero.");
                push(vm, float_val(da / db));
                release(a); release(b);
                break;
            }
            case OP_MOD: {
                Value b = pop(vm);
                Value a = pop(vm);
                Value res = eval_binop_values(vm->in, "%", a, b, 0);
                push(vm, res);
                release(a); release(b);
                break;
            }
            case OP_POW: {
                Value b = pop(vm);
                Value a = pop(vm);
                Value res = eval_binop_values(vm->in, "**", a, b, 0);
                push(vm, res);
                release(a); release(b);
                break;
            }
            case OP_NOT: {
                Value v = pop(vm);
                push(vm, bool_val(is_falsey(v)));
                release(v);
                break;
            }
            case OP_NEG: {
                Value v = pop(vm);
                if (v.kind == V_INT) push(vm, int_val(-v.as.i));
                else if (v.kind == V_FLOAT) push(vm, float_val(-v.as.f));
                release(v);
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                vm->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                Value condition = pop(vm);
                bool falsey = is_falsey(condition);
                release(condition);
                if (falsey) vm->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                vm->ip -= offset;
                break;
            }
            case OP_PRINT: {
                uint8_t arg_count = READ_BYTE();
                Value *args = (Value*)xmalloc(sizeof(Value) * arg_count);
                for (int i = arg_count - 1; i >= 0; i--) {
                    args[i] = pop(vm);
                }
                for (uint8_t i = 0; i < arg_count; i++) {
                    char *utf8 = value_to_utf8(vm->in, args[i]);
                    if (i > 0) vm->in->output(" ");
                    vm->in->output(utf8);
                    free(utf8);
                    release(args[i]);
                }
                free(args);
                vm->in->output("\n");
                break;
            }
            case OP_CALL_FUNC: {
                uint8_t nargs = READ_BYTE();
                Str *name_str = READ_STRING();
                char *name = str_to_utf8(name_str);
                Value *args = nargs ? (Value*)xmalloc(sizeof(Value) * nargs) : NULL;
                for (int i = (int)nargs - 1; i >= 0; i--) {
                    args[i] = pop(vm);
                }
                Value callee = pop(vm);
                Value res = call_value(vm->in, callee, args, nargs, name, NO_LINE);
                push(vm, res);
                if (args) free(args);
                release(callee);
                free(name);
                break;
            }
            case OP_CLOSURE: {
                Value constant = READ_CONSTANT();
                Node *node = (Node*)constant.as.o;
                if (node->kind == N_FUNCDEF) {
                    Value func_val = make_func_value(node->v.funcdef, vm->in->globals);
                    push(vm, func_val);
                } else {
                    push(vm, retain(constant));
                }
                break;
            }
            case OP_RETURN_VALUE: {
                Value result = pop(vm);
                push(vm, result);
                return INTERPRET_OK;
            }
            case OP_CLASS_DEF: {
                Value constant = READ_CONSTANT();
                Node *node = (Node*)constant.as.o;
                if (node->kind == N_CLASSDEF) {
                    define_class(vm->in, node->v.classdef, vm->in->globals, node->line);
                }
                break;
            }
            case OP_GET_PROPERTY: {
                Str *name_str = READ_STRING();
                char *name = str_to_utf8(name_str);
                Value target = pop(vm);
                Value res = member_of(vm->in, target, name, vm->in->globals, NO_LINE);
                push(vm, res);
                release(target);
                free(name);
                break;
            }
            case OP_SET_PROPERTY: {
                Str *name_str = READ_STRING();
                char *name = str_to_utf8(name_str);
                Value value = pop(vm);
                Value target = pop(vm);
                set_member(vm->in, target, name, value, vm->in->globals, NO_LINE);
                release(target);
                release(value);
                free(name);
                break;
            }
            case OP_INVOKE_METHOD: {
                Str *name_str = READ_STRING();
                char *name = str_to_utf8(name_str);
                uint8_t nargs = READ_BYTE();
                Value *args = nargs ? (Value*)xmalloc(sizeof(Value) * nargs) : NULL;
                for (int i = (int)nargs - 1; i >= 0; i--) {
                    args[i] = pop(vm);
                }
                Value target = pop(vm);
                Value fn = member_of(vm->in, target, name, vm->in->globals, NO_LINE);
                Value res = call_value(vm->in, fn, args, nargs, name, NO_LINE);
                push(vm, res);
                release(fn);
                release(target);
                if (args) free(args);
                free(name);
                break;
            }
            case OP_GET_SUPER: {
                Str *name_str = READ_STRING();
                char *name = str_to_utf8(name_str);
                Value *s = env_lookup(vm->in->globals, "super", NULL);
                if (!s) {
                    lumi_error(NO_LINE, "'super' can only be used inside a class method.");
                }
                Value res = member_of(vm->in, *s, name, vm->in->globals, NO_LINE);
                push(vm, res);
                free(name);
                break;
            }
            case OP_GET_ITER: {
                Value iterable = pop(vm);
                Seq *items = seq_new(V_LIST);
                if (iterable.kind == V_STR) {
                    Str *s = AS_STR(iterable);
                    for (size_t i = 0; i < s->len; i++) seq_push(items, obj_val(str_new(s->cp + i, 1)));
                } else if (iterable.kind == V_LIST || iterable.kind == V_TUPLE) {
                    Seq *s = AS_SEQ(iterable);
                    for (size_t i = 0; i < s->len; i++) seq_push(items, retain(s->items[i]));
                } else if (iterable.kind == V_BYTES) {
                    BytesObj *b = AS_BYTES(iterable);
                    for (size_t i = 0; i < b->len; i++) seq_push(items, int_val(b->data[i]));
                } else if (iterable.kind == V_DICT) {
                    Dict *d = AS_DICT(iterable);
                    for (size_t i = 0; i < d->len; i++) seq_push(items, retain(d->e[i].key));
                } else {
                    char *s = S(vm->in, iterable);
                    release(iterable);
                    lumi_error(NO_LINE, "Cannot iterate over '%s'", s);
                }
                release(iterable);
                /* Push seq object and current index (0) */
                push(vm, obj_val(items));
                push(vm, int_val(0));
                break;
            }
            case OP_FOR_ITER: {
                uint16_t offset = READ_SHORT();
                Value idx_val = pop(vm);
                Value seq_val = peek(vm, 0);
                Seq *seq = AS_SEQ(seq_val);
                long long idx = idx_val.as.i;
                if (idx < (long long)seq->len) {
                    push(vm, int_val(idx + 1));
                    push(vm, retain(seq->items[idx]));
                } else {
                    /* Iteration finished */
                    pop(vm); /* Pop seq_val */
                    release(seq_val);
                    vm->ip += offset;
                }
                break;
            }
            case OP_SLICE: {
                Value end_val = pop(vm);
                Value start_val = pop(vm);
                Value target = pop(vm);
                bool has_end = (end_val.kind != V_NONE);
                bool has_start = (start_val.kind != V_NONE);
                Value res = do_slice(vm->in, target, has_start ? &start_val : NULL, has_end ? &end_val : NULL, NO_LINE);
                push(vm, res);
                release(target); release(start_val); release(end_val);
                break;
            }
            case OP_UNPACK: {
                uint8_t expected = READ_BYTE();
                Value seq_val = pop(vm);
                if (seq_val.kind != V_LIST && seq_val.kind != V_TUPLE) {
                    release(seq_val);
                    lumi_error(NO_LINE, "Unpack requires a list or tuple.");
                }
                Seq *s = AS_SEQ(seq_val);
                if (s->len != expected) {
                    release(seq_val);
                    lumi_error(NO_LINE, "Unpack count mismatch.");
                }
                for (int i = (int)expected - 1; i >= 0; i--) {
                    push(vm, retain(s->items[i]));
                }
                release(seq_val);
                break;
            }
            case OP_PUSH_TRY: {
                uint16_t offset = READ_SHORT();
                if (vm->try_count < TRY_MAX) {
                    VMTryHandler *th = &vm->try_stack[vm->try_count++];
                    th->handler_ip_offset = (vm->ip + offset) - vm->chunk->code;
                    th->stack_depth = (size_t)(vm->stack_top - vm->stack);
                    th->frame_index = vm->frame_count;
                }
                break;
            }
            case OP_POP_TRY: {
                if (vm->try_count > 0) vm->try_count--;
                break;
            }
            case OP_THROW: {
                Value err = pop(vm);
                if (vm->try_count > 0) {
                    VMTryHandler th = vm->try_stack[--vm->try_count];
                    /* Reset stack top to entry depth */
                    while ((size_t)(vm->stack_top - vm->stack) > th.stack_depth) {
                        release(pop(vm));
                    }
                    vm->ip = vm->chunk->code + th.handler_ip_offset;
                    push(vm, err);
                } else {
                    char *err_str = value_to_utf8(vm->in, err);
                    lumi_error(NO_LINE, "%s", err_str);
                }
                break;
            }
            case OP_BUILD_LIST: {
                uint8_t count = READ_BYTE();
                Seq *list = seq_new(V_LIST);
                Value *elems = (Value*)xmalloc(sizeof(Value) * count);
                for (int i = count - 1; i >= 0; i--) {
                    elems[i] = pop(vm);
                }
                for (uint8_t i = 0; i < count; i++) {
                    seq_push(list, elems[i]);
                }
                free(elems);
                push(vm, obj_val(list));
                break;
            }
            case OP_BUILD_DICT: {
                uint8_t npairs = READ_BYTE();
                Dict *dict = dict_new();
                for (uint8_t i = 0; i < npairs; i++) {
                    Value val = pop(vm);
                    Value key = pop(vm);
                    dict_set(dict, key, val);
                }
                push(vm, obj_val(dict));
                break;
            }
            case OP_GET_INDEX: {
                Value index = pop(vm);
                Value target = pop(vm);
                if (target.kind == V_LIST || target.kind == V_TUPLE) {
                    Seq *s = AS_SEQ(target);
                    long long idx = index.as.i;
                    if (idx < 0) idx += s->len;
                    if (idx >= 0 && (size_t)idx < s->len) {
                        push(vm, retain(s->items[idx]));
                    } else {
                        lumi_error(NO_LINE, "Index out of range.");
                    }
                } else if (target.kind == V_DICT) {
                    Value *val = dict_find(AS_DICT(target), index);
                    if (val) push(vm, retain(*val));
                    else lumi_error(NO_LINE, "Key error in dict.");
                } else {
                    Value res = do_index(vm->in, target, index, 0);
                    push(vm, res);
                }
                release(target); release(index);
                break;
            }
            case OP_SET_INDEX: {
                Value val = pop(vm);
                Value index = pop(vm);
                Value target = pop(vm);
                if (target.kind == V_LIST) {
                    Seq *s = AS_SEQ(target);
                    long long idx = index.as.i;
                    if (idx < 0) idx += s->len;
                    if (idx >= 0 && (size_t)idx < s->len) {
                        release(s->items[idx]);
                        s->items[idx] = retain(val);
                    }
                } else if (target.kind == V_DICT) {
                    dict_set(AS_DICT(target), index, val);
                } else {
                    set_index(vm->in, target, index, val, 0);
                }
                release(target); release(index); release(val);
                break;
            }
            case OP_EVAL_AST: {
                Value node_val = READ_CONSTANT();
                Node *node = (Node*)node_val.as.o;
                Flow flow = FLOW_NORMAL;
                Value res = eval_ast_node(vm->in, node, vm->in->globals, &flow);
                if (flow == FLOW_RETURN) {
                    push(vm, res);
                    return INTERPRET_OK;
                } else {
                    if (is_expression_node(node)) {
                        push(vm, res);
                    } else {
                        release(res);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
}
