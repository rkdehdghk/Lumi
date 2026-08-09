/* Lumi Bytecode VM - Opcode Definitions
 * ==========================================================
 */
#ifndef OPCODE_H
#define OPCODE_H

typedef enum {
    OP_HALT = 0,
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    
    /* 변수 접근 */
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    
    /* 연산자 */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_NEG,
    OP_NOT,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    
    /* 제어 구조 */
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    
    /* 컬렉션 조작 */
    OP_BUILD_LIST,
    OP_BUILD_DICT,
    OP_GET_INDEX,
    OP_SET_INDEX,
    
    /* 내장 기능 및 함수 */
    OP_PRINT,
    OP_CALL,
    OP_CALL_FUNC,
    OP_CLOSURE,
    OP_RETURN,
    OP_RETURN_VALUE,
    
    /* 객체지향 (OOP) */
    OP_CLASS_DEF,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_INVOKE_METHOD,
    OP_GET_SUPER,

    /* 반복자 (Iterators) */
    OP_GET_ITER,
    OP_FOR_ITER,

    /* 슬라이싱 및 언팩 */
    OP_SLICE,
    OP_UNPACK,

    /* 예외 처리 (Exceptions) */
    OP_PUSH_TRY,
    OP_POP_TRY,
    OP_THROW,

    OP_EVAL_AST
} OpCode;

#endif /* OPCODE_H */
