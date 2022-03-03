import ctypes
from dataclasses import dataclass
import clang.cindex as cindex
import enum
import json
import sys
import os
import time
import z3
from typing import Any, Callable, Dict, List, Iterator, Optional, Set, Tuple, TypeVar


@dataclass
class SerializedTU:
    serialization_time: int
    assertions: List[str]
    solver: List[Any]


class WalkResult(enum.Enum):
    # Stop walking.
    BREAK = 1

    # Skip the children of this node.
    CONTINUE = 2

    # Visit the node's children.
    RECURSE = 3


class LogLevel(enum.Enum):
    INFO = 1
    WARNING = 2
    ERROR = 3


T = TypeVar('T')


def walk_ast(cursor: cindex.Cursor, callback: Callable[[cindex.Cursor, T], WalkResult], data: T = None):
    for child in cursor.get_children():
        result = callback(child, data)
        if result == WalkResult.BREAK:
            break
        elif result == WalkResult.RECURSE:
            walk_ast(child, callback, data)


def _get_fq_varname(cursor: cindex.Cursor) -> str:
    fq_name = ''
    if cursor.linkage == cindex.LinkageKind.NO_LINKAGE:
        fq_name = cursor.spelling + \
            f'_{cursor.location.file}_{cursor.location.line}'
    else:
        fq_name = cursor.spelling
    # fq_name = cursor.spelling + f'_{cursor.location.file}'
    c: cindex.Cursor = cursor.lexical_parent
    while c is not None and not cindex.CursorKind.is_translation_unit(c.kind):
        fq_name = c.spelling + '_' + fq_name
        c = c.lexical_parent
    return fq_name


def get_fq_name(cursor: cindex.Cursor) -> str:
    if cursor.kind == cindex.CursorKind.VAR_DECL:
        return _get_fq_varname(cursor)

    c: cindex.Cursor = cursor.semantic_parent
    fq_method_name = cursor.spelling
    prev = cursor
    while c is not None and c != prev and not cindex.CursorKind.is_translation_unit(c.kind):
        fq_method_name = c.spelling + '::' + fq_method_name
        prev = c
        c = c.semantic_parent
    return fq_method_name


def get_binary_op(cursor: cindex.Cursor) -> str:
    try:
        children_list = [i for i in cursor.get_children()]
        left_offset = len([i for i in children_list[0].get_tokens()])
        op = [i for i in cursor.get_tokens()][left_offset].spelling
        return op
    except Exception:
        return ''


def get_unary_op(cursor: cindex.Cursor) -> str:
    try:
        return list(cursor.get_tokens())[0].spelling
    except Exception:
        return ''


def is_assignment_operator(cursor: cindex.Cursor) -> bool:
    return (cursor.kind == cindex.CursorKind.BINARY_OPERATOR
            and get_binary_op(cursor) == '=') or (cursor.kind == cindex.CursorKind.CALL_EXPR and
                                                  cursor.spelling == '=')


def _get_lhs_helper(cursor: cindex.Cursor, data: Dict[Any, Any]) -> WalkResult:
    if data.get('result'):
        return WalkResult.BREAK
    if cursor.kind != cindex.CursorKind.UNEXPOSED_EXPR:
        data['result'] = cursor
        return WalkResult.BREAK
    return WalkResult.RECURSE


def get_lhs(cursor: cindex.Cursor) -> cindex.Cursor:
    data = {}
    walk_ast(cursor, _get_lhs_helper, data)
    return data['result']


def _get_rhs_helper(cursor: cindex.Cursor, data: Dict[Any, Any]) -> WalkResult:
    if data.get('visited') is None:
        data['visited'] = True
        return WalkResult.CONTINUE
    if cursor.kind != cindex.CursorKind.UNEXPOSED_EXPR:
        data['result'] = cursor
        return WalkResult.BREAK
    return WalkResult.RECURSE


def get_rhs(cursor: cindex.Cursor) -> cindex.Cursor:
    data = {}
    walk_ast(cursor, _get_rhs_helper, data)
    return data['result']


def get_integer_literal(cursor: cindex.Cursor) -> int:
    assert cursor.kind == cindex.CursorKind.INTEGER_LITERAL or cursor.kind == cindex.CursorKind.CXX_BOOL_LITERAL_EXPR
    cindex.conf.lib.clang_Cursor_Evaluate.restype = ctypes.c_void_p
    result = ctypes.c_void_p(cindex.conf.lib.clang_Cursor_Evaluate(cursor))
    val = cindex.conf.lib.clang_EvalResult_getAsInt(result)
    cindex.conf.lib.clang_EvalResult_dispose(result)
    return val


def get_floating_literal(cursor: cindex.Cursor) -> float:
    assert cursor.kind == cindex.CursorKind.FLOATING_LITERAL
    cindex.conf.lib.clang_Cursor_Evaluate.restype = ctypes.c_void_p
    result = ctypes.c_void_p(cindex.conf.lib.clang_Cursor_Evaluate(cursor))
    cindex.conf.lib.clang_EvalResult_getAsDouble.restype = ctypes.c_double
    val = cindex.conf.lib.clang_EvalResult_getAsDouble(result)
    cindex.conf.lib.clang_EvalResult_dispose(result)
    return val


def _get_arguments_helper(cursor: cindex.Cursor, data: List[cindex.Cursor]) -> WalkResult:
    if cursor.kind != cindex.CursorKind.UNEXPOSED_EXPR and len(data) == 0:
        data.append(cursor)
        return WalkResult.BREAK
    return WalkResult.RECURSE


def get_arguments(cursor: cindex.Cursor) -> Iterator[cindex.Cursor]:
    for child in cursor.get_arguments():
        args = []
        if child.kind != cindex.CursorKind.UNEXPOSED_EXPR:
            args.append(child)
        else:
            walk_ast(child, _get_arguments_helper, args)
        yield args[0] if len(args) > 0 else None


def plain_type(t: cindex.Type):
    t1 = t
    while t1.kind == cindex.TypeKind.POINTER:
        t1 = t1.get_pointee()
    if t1.is_const_qualified() or t1.is_restrict_qualified() or t1.is_volatile_qualified():
        t1 = t1.get_named_type()
    return t1


def _get_fq_member_expr_helper(cursor: cindex.Cursor, data: List[str]) -> WalkResult:
    if cursor.kind == cindex.CursorKind.DECL_REF_EXPR:
        t: cindex.Type = cursor.type
        if t.kind == cindex.TypeKind.CONSTANTARRAY:
            data[0] = cursor.spelling + data[0]
            return WalkResult.RECURSE

        typename: str = plain_type(t).spelling
        if len(typename.split(' ')) > 0 and typename.split(' ')[0] == 'struct':
            typename = typename[6:]
        data[0] = typename + '.' + data[0]
        return WalkResult.RECURSE
    elif cursor.kind == cindex.CursorKind.MEMBER_REF_EXPR:
        data[0] = cursor.spelling + '.' + data[0]
        return WalkResult.RECURSE
    elif cursor.kind == cindex.CursorKind.ARRAY_SUBSCRIPT_EXPR:
        data[0] = cursor.spelling + '.' + data[0]
    return WalkResult.RECURSE


def get_fq_member_expr(cursor: cindex.Cursor) -> str:
    data = [cursor.spelling]
    walk_ast(cursor, _get_fq_member_expr_helper, data)
    if not '.' in data[0] and cursor.referenced is not None:
        data[0] = cursor.referenced.semantic_parent.spelling + '.' + data[0]
    return data[0]


def log(level: LogLevel, *args):
    level_to_str = {
        LogLevel.INFO: 'INFO:',
        LogLevel.WARNING: 'WARNING:',
        LogLevel.ERROR: 'ERROR:',
    }
    print(level_to_str[level], sep='', end=' ', file=sys.stderr)
    print(*args, flush=True, file=sys.stderr)


def has_return_statement(cursor: cindex.Cursor) -> bool:
    '''Returns if the cursor has a return statement.'''
    def has_return_statement_walker(cursor: cindex.Cursor, data: Dict[Any, Any]):
        if cursor.kind == cindex.CursorKind.RETURN_STMT:
            data['HasReturn'] = True
            return WalkResult.BREAK
        return WalkResult.RECURSE

    data = {}
    walk_ast(cursor, has_return_statement_walker, data)
    return data.get('HasReturn', False)


def get_next_decl_ref_expr(cursor: cindex.Cursor) -> Optional[cindex.Cursor]:
    def walker(cursor: cindex.Cursor, data: Dict[str, cindex.Cursor]) -> WalkResult:
        if data.get('Decl') is None and cursor.kind == cindex.CursorKind.DECL_REF_EXPR:
            data['Decl'] = cursor
            return WalkResult.BREAK
        return WalkResult.RECURSE

    data = {}
    walk_ast(cursor, walker, data)
    return data.get('Decl')


def maybe_get_constrained_object(cursor: cindex.Cursor, frame_accesses: Set[str]) -> Optional[str]:
    '''Returns the name of the object whose frame is constrained if cursor constrains a frame.'''
    the_member_access = get_lhs(cursor)
    if the_member_access.kind != cindex.CursorKind.MEMBER_REF_EXPR:
        the_member_access = get_rhs(cursor)

    if the_member_access.kind == cindex.CursorKind.MEMBER_REF_EXPR:
        access = get_fq_member_expr(the_member_access)
        if access in frame_accesses:
            return get_fq_name(get_next_decl_ref_expr(the_member_access))


def maybe_get_constraint_literal(cursor: cindex.Cursor) -> Optional[int]:
    the_literal = get_lhs(cursor)
    if the_literal.kind != cindex.CursorKind.INTEGER_LITERAL:
        the_literal = get_rhs(cursor)

    if the_literal.kind == cindex.CursorKind.INTEGER_LITERAL:
        return get_integer_literal(the_literal)


def _translation_unit_to_filename(tu: cindex.TranslationUnit) -> str:
    return tu.spelling.replace('/', '_')


def serialize_tu(path: str, tu: cindex.TranslationUnit, tu_solver: z3.Solver, tu_assertions: List[z3.BoolRef]):
    '''Saves the translation unit's solver to a file.'''
    with open(os.path.join(path, _translation_unit_to_filename(tu) + '.json'), 'w') as f:
        serialized_obj = {
            'Assertions': [str(a) for a in tu_assertions],
            'SerializationTime': int(time.time()),
            'Solver': tu_solver.to_smt2(),
        }
        json.dump(serialized_obj, f)


def read_tu(path: str, file_path: str) -> SerializedTU:
    try:
        with open(os.path.join(path, file_path.replace('/', '_') + '.json')) as f:
            data = json.load(f)
            return SerializedTU(data['SerializationTime'], data['Assertions'], data['Solver'])
    except Exception:
        return SerializedTU(0, [], [])
