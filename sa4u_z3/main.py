import argparse
import ccsyspath
from pyexpat import model
import clang.cindex as cindex
import json
import os.path
import time
import xml.etree.ElementTree as ET
from typing import Any, Dict, Iterator, Optional, Set, TextIO, Tuple
from util import *
from z3 import *

# 0 is m, 1 is s, 2 is mole, 3 is amp, 4 is kelvin, 5 is cd, 6 is kg
NUM_BASE_UNITS = 7

# Maps frame names to a value.
MAV_FRAME_TO_ID = {
    'GLOBAL': 0,
    'LOCAL': 1,
    'MAV_FRAME_GLOBAL': 0,
    'MAV_FRAME_LOCAL_NED': 1,
    'MAV_FRAME_MISSION': 2,
    'MAV_FRAME_GLOBAL_RELATIVE_ALT': 3,
    'MAV_FRAME_LOCAL_ENU': 4,
    'MAV_FRAME_GLOBAL_INT': 5,
    'MAV_FRAME_GLOBAL_RELATIVE_ALT_INT': 6,
    'MAV_FRAME_LOCAL_OFFSET_NED': 7,
    'MAV_FRAME_BODY_NED': 8,
    'MAV_FRAME_BODY_OFFSET_NED': 9,
    'MAV_FRAME_GLOBAL_TERRAIN_ALT': 10,
    'MAV_FRAME_GLOBAL_TERRAIN_ALT_INT': 11,
    'MAV_FRAME_BODY_FRD': 12,
    'MAV_FRAME_LOCAL_FRD': 20,
    'MAV_FRAME_LOCAL_FLU': 21,
}

# The number of frames there are.
NUM_FRAMES = len(list(MAV_FRAME_TO_ID.keys()))

# The maximum number of parameters one of our functions can accept.
# MAX_FUNCTION_PARAMETERS = 42
MAX_FUNCTION_PARAMETERS = 0

# Maps unit names to their base vector.
UNIT_TO_BASE_UNIT_VECTOR = {
    'centimeter': [1, 0, 0, 0, 0, 0, 0],
    'cm': [1, 0, 0, 0, 0, 0, 0],
    'cm/s': [1, -1, 0, 0, 0, 0, 0],
    'cm^2': [2, 0, 0, 0, 0, 0, 0],
    'gauss': [0, -2, 0, -1, 0, 0, 1],
    'literal': [0, 0, 0, 0, 0, 0, 0],
    'm': [1, 0, 0, 0, 0, 0, 0],
    'mgauss': [0, -2, 0, -1, 0, 0, 1],
    'meter': [1, 0, 0, 0, 0, 0, 0],
    'mm': [1, 0, 0, 0, 0, 0, 0],
    'ms': [0, 1, 0, 0, 0, 0, 0],
    'm/s': [1, -1, 0, 0, 0, 0, 0],
    'm/s/s': [1, -2, 0, 0, 0, 0, 0],
    's': [0, 1, 0, 0, 0, 0, 0],
    'us': [0, 1, 0, 0, 0, 0, 0],
}

# Maps unit names to their scalar multiplier.
# The first component is the numerator.
# The second component is the denominator.
UNIT_TO_SCALAR = {
    'centimeter': (1, 100),
    'cm': (1, 100),
    'cm/s': (1, 100),
    'cm^2': (1, 10000),
    'gauss': (1, 1000),
    'meter': (1, 1),
    'm': (1, 1),
    'mgauss': (1, 10000000),
    'mm': (1, 1000),
    'ms': (1, 1000),
    'm/s': (1, 1),
    'm/s/s': (1, 1),
    's': (1, 1),
    'us': (1, 1000000),
}

# Stores the return type of functions.
# TODO: can I provide better type information?
_fn_name_to_return_type: Dict[str, DatatypeRef] = {}

# Stores the types of variables.
# TODO: can I provide better type information?
_var_name_to_type: Dict[str, DatatypeRef] = {}

# Stores if a type has any unit information associated with it.
_typename_has_unit: Dict[str, bool] = {}

# Relates a static member access to type information.
_member_access_to_type: Dict[str, DatatypeRef] = {}

# A datatype that stores type information.
Type: DatatypeSortRef

# A datatype that stores the dimension of a unit.
Unit: DatatypeSortRef

# A datatype that stores the possible frames of a unit.
Frames: DatatypeSortRef

# A datatype that stores a rational number.
Rational: DatatypeSortRef

# A function that associates a function argument with a type.
ArgType: FuncDeclRef

# Global variable: the Z3 solving context.
solver: Solver

# Stores a counter for creating unique messages and labeling expressions.
_counter = 0

# Stores if we should use a power-of-ten representation.
_use_power_of_ten: bool = False

# Stores if we should use scalar prefixes.
_enable_scalar_prefixes: bool = True

# Stores member access strings with prior known types.
_member_access_with_prior_types: Set[str] = set([])

# Functions to ignore.
_IGNORE_FUNCS = {
    'AP_Logger_Backend::Write_Message',
    'calloc',
    'malloc',
    'printf',
    'puts',
    'is_zero',
    'is_positive',
}

# Member assignments to ignore.
_IGNORE_MEMBERS = {
    'mavlink_mission_item_t.param1',
    'mavlink_mission_item_t.param2',
    'mavlink_mission_item_t.param3',
    'mavlink_mission_item_t.param4',
    'mavlink_mission_item_t.x',
    'mavlink_mission_item_t.y',
    'mavlink_mission_item_t.z',
}


def main():
    global _enable_scalar_prefixes, _use_power_of_ten

    parser = argparse.ArgumentParser(
        description='checks source code for unit conversion errors',
    )
    parser.add_argument(
        '--compilation-database',
        '-c',
        dest='compilation_database_path',
        help='path to the directory containing a compile_commands.json file',
        required=True,
        type=str,
    )
    parser.add_argument(
        '-m',
        '--message-definition',
        dest='message_definition_path',
        help='path to XML file containing the message spec: supported specs are MavLink and LMCP',
        required=True,
        type=str,
    )
    parser.add_argument(
        '-p',
        '--prior-types',
        dest='prior_types_path',
        help='path to JSON file describing previously known types',
        required=True,
        type=str,
    )
    parser.add_argument(
        '--power-of-10',
        dest='power_of_ten',
        help='use a power of 10 representation of unit scalars',
        required=False,
        type=bool,
        default=False,
    )
    parser.add_argument(
        '--disable-scalar-prefixes',
        dest='disable_scalar_prefixes',
        help='do not use scalar prefixes. can speed up analysis.',
        required=False,
        type=bool,
        default=False,
    )
    parsed_args = parser.parse_args()

    _use_power_of_ten = parsed_args.power_of_ten
    _enable_scalar_prefixes = not parsed_args.disable_scalar_prefixes

    initialize_z3()

    with open(parsed_args.prior_types_path, 'r') as prior_types_fd:
        load_prior_types(prior_types_fd)

    with open(parsed_args.message_definition_path, 'r') as message_def_fd:
        load_message_definitions(message_def_fd)

    compilation_database: cindex.CompilationDatabase = cindex.CompilationDatabase.fromDirectory(
        parsed_args.compilation_database_path,
    )

    start = time.time()
    count = 0
    for tu in translation_units(compilation_database):
        count += 1
        log(
            LogLevel.INFO,
            f'{count} / {len(compilation_database.getAllCompileCommands())}',
        )
        cursor: cindex.Cursor = tu.cursor
        walk_ast(cursor, walker, {'Seen': set([])})
    end = time.time()
    print(f'Parsing elapsed time: {end - start} seconds', flush=True)

    # with open('smt_out', 'w') as fd:
    #     print(solver.to_smt2(), file=fd)
    #     exit(1)

    # for assertion in solver.assertions():
    #     print(assertion)

    start = time.time()
    status = solver.check()
    end = time.time()
    print(f'Z3 elapsed time: {end - start} seconds', flush=True)
    if status != sat:
        print('ERROR!')
        core = solver.unsat_core()
        for failure in core:
            print(f'  {failure}')

    # for m in solver.model():
    #     print(f'{m} = {solver.model()[m]}')


def walker(cursor: cindex.Cursor, data: Dict[Any, Any]) -> WalkResult:
    global _counter
    if cursor.location.file is not None:
        home = os.getenv('HOME')
        filename: str = cursor.location.file.name
        if not filename.startswith(home) and not filename.startswith('/src/'):
            return WalkResult.CONTINUE

    cursor_descr = f'{cursor.location.line}_{cursor.location.column}_{cursor.get_usr()}'
    if cursor_descr in data['Seen']:
        return WalkResult.CONTINUE
    data['Seen'].add(cursor_descr)

    if cursor.kind == cindex.CursorKind.FUNCTION_DECL or cursor.kind == cindex.CursorKind.CXX_METHOD:
        data['CurrentFn'] = get_fq_name(cursor)
        data['next_id'] = 0
        data['param_names_to_id'] = {}
        return WalkResult.RECURSE
    elif cursor.kind == cindex.CursorKind.PARM_DECL:
        if data.get('param_names_to_id') is None:
            data['param_names_to_id'] = {}
            data['next_id'] = 0
        data['param_names_to_id'][cursor.spelling] = data['next_id']
        data['next_id'] += 1
        return WalkResult.CONTINUE
    elif cursor.kind == cindex.CursorKind.VAR_DECL:
        # We have an uninitialized variable. Skip it for now.
        if len(list(cursor.get_children())) == 0:
            return WalkResult.CONTINUE

        # Weird, but correct. The first child is the true right hand side.
        # get_lhs returns the first child, so this does what we want.
        rhs_type = type_expr(get_lhs(cursor), data)
        if rhs_type is None:
            return WalkResult.CONTINUE

        lhs_typename = f'{get_fq_name(cursor)}_type'
        if lhs_typename not in _var_name_to_type:
            _var_name_to_type[lhs_typename] = Const(lhs_typename, Type)

        assert_and_check(
            _var_name_to_type[lhs_typename] == rhs_type,
            f'Variable {cursor.spelling} declared in {cursor.location.file} on line {cursor.location.line} ({_counter})',
        )
        _counter += 1

        return WalkResult.CONTINUE
    elif is_assignment_operator(cursor):
        lhs_type = type_expr(get_lhs(cursor), data)
        if lhs_type is None:
            log(
                LogLevel.WARNING,
                f'unrecognized lhs type @ {cursor.location.file} line {cursor.location.line}',
            )
            return WalkResult.CONTINUE

        rhs_type = type_expr(get_rhs(cursor), data)
        if rhs_type is None:
            log(
                LogLevel.WARNING,
                f'unrecognized rhs type @ {cursor.location.file} line {cursor.location.line}',
            )
            return WalkResult.CONTINUE

        assert_and_check(
            Or(
                lhs_type == rhs_type,
                And(
                    is_dimensionless(lhs_type),
                    is_dimensionless(rhs_type),
                )
            ),
            f'Assignment to {get_lhs(cursor).spelling} in {cursor.location.file} on line {cursor.location.line} column {cursor.location.column} ({_counter})',
        )
        _counter += 1
        return WalkResult.CONTINUE
    elif cursor.kind == cindex.CursorKind.CALL_EXPR:
        # TODO: methods are actually a mess.
        if cursor.referenced is None:
            return WalkResult.RECURSE

        fq_fn_name = get_fq_name(cursor.referenced)
        if fq_fn_name in _IGNORE_FUNCS:
            return WalkResult.CONTINUE

        func_name = String(fq_fn_name)
        for arg, arg_no in zip(get_arguments(cursor), range(0, 1000)):
            if arg is None:
                log(
                    LogLevel.WARNING,
                    f'no argument cursor found in {cursor.location.file} on line {cursor.location.line}',
                )
                continue

            arg_type = type_expr(arg, data)
            if arg_type is None:
                log(
                    LogLevel.WARNING,
                    f'unknown argument type in {cursor.location.file} on line {cursor.location.line}',
                )
                return WalkResult.RECURSE
            assert_and_check(
                type_expr(arg, data) == ArgType(func_name, arg_no),
                f'Call to {func_name.as_string()} in {cursor.location.file} on line {cursor.location.line} column {cursor.location.column} ({_counter})',
            )
            _counter += 1
        return WalkResult.RECURSE
    return WalkResult.RECURSE


def kind_printer(cursor: cindex.Cursor, _) -> WalkResult:
    print(f'kind: {cursor.kind} {cursor.spelling}')
    return WalkResult.RECURSE


def type_expr(cursor: cindex.Cursor, context: Dict[Any, Any]) -> Optional[DatatypeRef]:
    global _counter
    if cursor.kind == cindex.CursorKind.CALL_EXPR:
        if cursor.referenced is None:
            log(
                LogLevel.WARNING,
                f'unknown call to {cursor.spelling} in {cursor.location.file} on line {cursor.location.line} column {cursor.location.column}',
            )
            return

        fq_fn_name = get_fq_name(cursor.referenced)
        if fq_fn_name in _IGNORE_FUNCS:
            return

        reference_typename = f'{get_fq_name(cursor.referenced)}_return_type'
        if _fn_name_to_return_type.get(reference_typename) is None:
            _fn_name_to_return_type[reference_typename] = Const(
                reference_typename,
                Type,
            )

        return _fn_name_to_return_type.get(reference_typename)
    elif cursor.kind == cindex.CursorKind.VARIABLE_REF or cursor.kind == cindex.CursorKind.DECL_REF_EXPR:
        if context.get('CurrentFn') and cursor.spelling in context.get('param_names_to_id', {}):
            return ArgType(
                String(context['CurrentFn']),
                context['param_names_to_id'][cursor.spelling],
            )
        if cursor.referenced is None:
            return

        var_typename = f'{get_fq_name(cursor.referenced)}_type'
        if _var_name_to_type.get(var_typename) is None:
            _var_name_to_type[var_typename] = Const(var_typename, Type)
        return _var_name_to_type[var_typename]
    elif cursor.kind == cindex.CursorKind.UNEXPOSED_EXPR:
        log(
            LogLevel.WARNING,
            f'calling type_expr on UNEXPOSED_EXPR.',
        )
    elif cursor.kind == cindex.CursorKind.BINARY_OPERATOR:
        operator = get_binary_op(cursor)
        if operator == '+' or operator == '-':
            # The LHS and RHS must share a type.
            lhs_type = type_expr(get_lhs(cursor), context)
            rhs_type = type_expr(get_rhs(cursor), context)
            loc: cindex.SourceLocation = cursor.location
            if lhs_type is None or rhs_type is None:
                log(
                    LogLevel.WARNING,
                    f'untyped expression @ {loc.file} line {loc.line}',
                )
                return None
            assert_and_check(
                Or(
                    lhs_type == rhs_type,
                    is_dimensionless(lhs_type),
                    is_dimensionless(rhs_type),
                ),
                f'Applied {operator} with incompatible types @ {loc.file} line {loc.line} column {loc.column} ({_counter})',
            )
            _counter += 1
            return lhs_type
        elif operator == '*' or operator == '/':
            # There is a new type, equal to the product of the lhs and rhs.
            lhs_type = type_expr(get_lhs(cursor), context)
            rhs_type = type_expr(get_rhs(cursor), context)
            loc: cindex.SourceLocation = cursor.location
            if lhs_type is None or rhs_type is None:
                log(
                    LogLevel.WARNING,
                    f'untyped expression @ {loc.file} on line {loc.line}',
                )
                return None

            assert_and_check(
                Type.frame(lhs_type) == Type.frame(rhs_type),
                f'Frames must agree in operator {operator} applied in {cursor.location.file} on line {cursor.location.line} ({_counter})',
            )
            _counter += 1

            product_type = Type.type(
                create_unit(
                    scalar_multiply(
                        get_scalar(Type.unit(lhs_type)),
                        get_scalar(Type.unit(rhs_type)),
                    ) if operator == '*'
                    else scalar_divide(
                        get_scalar(Type.unit(lhs_type)),
                        get_scalar(Type.unit(rhs_type)),
                    ),
                    *[
                        Unit.__dict__[f'si_base_unit_{i}'](Type.unit(lhs_type)) +
                        Unit.__dict__[f'si_base_unit_{i}'](Type.unit(rhs_type))
                        if operator == '*'
                        else
                        Unit.__dict__[f'si_base_unit_{i}'](Type.unit(lhs_type)) -
                        Unit.__dict__[f'si_base_unit_{i}'](Type.unit(rhs_type))
                        for i in range(NUM_BASE_UNITS)
                    ],
                    *[
                        Unit.__dict__[f'p_{i}'](Type.unit(lhs_type)) +
                        Unit.__dict__[f'p_{i}'](Type.unit(rhs_type))
                        if operator == '*'
                        else Unit.__dict__[f'p_{i}'](Type.unit(lhs_type)) -
                        Unit.__dict__[f'p_{i}'](Type.unit(rhs_type))
                        for i in range(MAX_FUNCTION_PARAMETERS)
                    ],
                ),
                Type.frame(lhs_type),
            )
            _counter += 1
            return product_type
        # TODO: handle other binary operators.
    elif cursor.kind == cindex.CursorKind.INTEGER_LITERAL or cursor.kind == cindex.CursorKind.CXX_BOOL_LITERAL_EXPR:
        # Could be any type. Let it be.
        literal_type = Type.type(
            create_unit(
                create_scalar_instance(num=get_integer_literal(cursor)),
                *UNIT_TO_BASE_UNIT_VECTOR['literal'],
                *([0] * MAX_FUNCTION_PARAMETERS),
            ),
            #FreshConst(Unit, 'literal_unit'),
            FreshConst(Frames, 'literal_frames'),
        )
        return literal_type
    elif cursor.kind == cindex.CursorKind.FLOATING_LITERAL:
        return Type.type(
            create_unit(
                create_scalar_instance(num=get_floating_literal(cursor)),
                *UNIT_TO_BASE_UNIT_VECTOR['literal'],
                *([0] * MAX_FUNCTION_PARAMETERS),
            ),
            #FreshConst(Unit, 'literal_unit'),
            FreshConst(Frames, 'literal_frames'),
        )
    elif cursor.kind == cindex.CursorKind.MEMBER_REF_EXPR or cursor.kind == cindex.CursorKind.ARRAY_SUBSCRIPT_EXPR:
        expr_repr = get_fq_member_expr(cursor)
        if expr_repr in _IGNORE_MEMBERS:
            return
        t = _member_access_to_type.get(expr_repr)
        if t is None:
            t = FreshConst(Type, 'member accessed')
            _member_access_to_type[
                get_fq_member_expr(cursor)
            ] = t
        return t
    elif cursor.kind == cindex.CursorKind.UNARY_OPERATOR:
        operator = get_unary_op(cursor)
        if operator == '-':
            return type_expr(get_lhs(cursor), context)
        elif operator == '&':
            return type_expr(get_lhs(cursor), context)
    elif cursor.kind == cindex.CursorKind.PAREN_EXPR:
        return type_expr(get_lhs(cursor), context)
    elif cursor.kind == cindex.CursorKind.CSTYLE_CAST_EXPR:
        return type_expr(get_lhs(cursor), context)
    # else:
    #     print(
    #         f'type_expr(): unrecognized cursor: {cursor.kind} in {cursor.location.file} on line {cursor.location.line}')


def scalar_multiply(s1: DatatypeRef, s2: DatatypeRef) -> DatatypeRef:
    if _use_power_of_ten:
        return s1 + s2
    else:
        return _rational_multiply(s1, s2)


def scalar_divide(s1: DatatypeRef, s2: DatatypeRef) -> DatatypeRef:
    if _use_power_of_ten:
        return s1 - s2
    else:
        return _rational_divide(s1, s2)


def _rational_multiply(r1: DatatypeRef, r2: DatatypeRef) -> DatatypeRef:
    return Rational.rational(
        Rational.numerator(r1) * Rational.numerator(r2),
        Rational.denominator(r1) * Rational.denominator(r2),
    )


def _rational_divide(r1: DatatypeRef, r2: DatatypeRef) -> DatatypeRef:
    return Rational.rational(
        Rational.numerator(r1) * Rational.denominator(r2),
        Rational.denominator(r1) * Rational.numerator(r2),
    )


def is_dimensionless(t: DatatypeRef) -> BoolRef:
    return And(
        *[
            Unit.__dict__[f'si_base_unit_{i}'](
                Type.unit(t),
            ) == 0
            for i in range(NUM_BASE_UNITS)
        ],
    )


def translation_units(compile_commands: cindex.CompilationDatabase) -> Iterator[cindex.TranslationUnit]:
    '''Returns an iterator over a translation unit for each file in the compilation database.'''
    compile_command: cindex.CompileCommand
    for compile_command in compile_commands.getAllCompileCommands():
        log(
            LogLevel.INFO,
            f'parsing {compile_command.filename}'
        )
        try:
            if 'lua' in compile_command.filename:
                continue
            os.chdir(compile_command.directory)
            translation_unit = cindex.TranslationUnit.from_source(
                os.path.join(compile_command.directory,
                             compile_command.filename),
                args=[arg for arg in compile_command.arguments
                      if arg != compile_command.filename] + ['-I' + inc.decode() for inc in ccsyspath.system_include_paths('clang')],
            )
            for diag in translation_unit.diagnostics:
                log(
                    LogLevel.WARNING,
                    f'Parsing: {compile_command.filename}: {diag}'
                )
            yield translation_unit
        except cindex.TranslationUnitLoadError:
            log(
                LogLevel.WARNING,
                f'could not parse {os.path.join(compile_command.directory, compile_command.filename)}',
            )


def create_scalar_instance(num: int = None, pair: Tuple[int, int] = None) -> DatatypeRef:
    if num is not None:
        if _use_power_of_ten:
            # return RealVal('{:.20f}'.format(math.log10(1 / num))) if num > 0 else RealVal(num)
            return IntVal(math.log10(1 / num) if num > 0 else num)
        else:
            return Rational.rational(1, num)
    elif pair is not None:
        if _use_power_of_ten:
            # return RealVal('{:.20f}'.format(math.log10(pair[0]) - math.log10(pair[1])))
            return IntVal(math.log10(pair[0]) - math.log10(pair[1]))
        else:
            return Rational.rational(pair[0], pair[1])
    else:
        raise RuntimeError(
            'No arguments provided to create_scalar_instance().')


def declare_types():
    global Frames, Rational, Type, TypeOf, Unit

    scalar_dt: DatatypeSortRef
    if _use_power_of_ten:
        #scalar_dt = RealSort()
        scalar_dt = IntSort()
    else:
        Rational = Datatype('Rational')
        Rational.declare(
            'rational',
            ('numerator', IntSort()),
            ('denominator', IntSort()),
        )
        Rational = Rational.create()
        scalar_dt = Rational

    Unit = Datatype('Unit')
    if _enable_scalar_prefixes:
        Unit.declare(
            'unit',
            ('scalar', scalar_dt),
            *[(f'si_base_unit_{i}', IntSort()) for i in range(NUM_BASE_UNITS)],
            *[(f'p_{i}', IntSort()) for i in range(MAX_FUNCTION_PARAMETERS)],
        )
    else:
        Unit.declare(
            'unit',
            *[(f'si_base_unit_{i}', IntSort()) for i in range(NUM_BASE_UNITS)],
            *[(f'p_{i}', IntSort()) for i in range(MAX_FUNCTION_PARAMETERS)],
        )
    Unit = Unit.create()

    Frames = Datatype('Frames')
    Frames.declare(
        'frames',
        *[(f'frame_{i}', BoolSort()) for i in range(NUM_FRAMES)],
    )
    Frames = Frames.create()

    Type = Datatype('Type')
    Type.declare(
        'type',
        ('unit', Unit),
        ('frame', Frames),
    )
    Type = Type.create()


def initialize_z3():
    '''
    Initializes z3.

    Returns: The solver to solve the typing constraints.
    '''
    global ArgType, Type, solver
    declare_types()

    ArgType = Function('ArgType', StringSort(), IntSort(), Type)

    solver = Solver()
    solver.set(unsat_core=True)
    solver.set(threads=4)


def load_prior_types(io: TextIO):
    data = json.load(io)
    for variable_description in data:
        parse_variable_description(variable_description)


def set_to_z3_set(set: Set[AstRef], sort: SortRef) -> ArrayRef:
    s = EmptySet(sort)
    for item in set:
        s = SetAdd(s, item)
    return s


def assert_and_check(stmt: Any, msg: str):
    # solver.push()
    solver.assert_and_track(stmt, msg)
    # if solver.check() == unsat:
    #     for problem in solver.unsat_core():
    #         log(
    #             LogLevel.ERROR,
    #             problem,
    #         )
    #     solver.pop()


def get_scalar(unit: DatatypeRef) -> DatatypeRef:
    if _enable_scalar_prefixes:
        return Unit.scalar(unit)
    elif _use_power_of_ten:
        return IntVal(0)
    else:
        return Rational.rational(1, 1)


def create_unit(scalar_prefix: DatatypeRef, *args) -> DatatypeRef:
    if _enable_scalar_prefixes:
        return Unit.unit(
            scalar_prefix,
            *args,
        )
    else:
        return Unit.unit(
            *args,
        )


def parse_variable_description(description: Dict[str, Any]):
    'Adds the variable descriptions to the solver.'
    global _member_access_with_prior_types, Frames, Type, Unit
    name: str = description['VariableName']
    name = name.replace('::', '.')
    _member_access_with_prior_types.add(name)

    variable_unit = Const(f'{name}_units', Unit)
    variable_frames = Const(f'{name}_frames', Frames)
    variable_type = Const(f'{name}_type', Type)

    scalar = UNIT_TO_SCALAR.get(description['SemanticInfo']['Units'][0])
    if scalar is None:
        return

    description_frames = set(
        [MAV_FRAME_TO_ID[frame_name]
            for frame_name in description['SemanticInfo']['CoordinateFrames']]
    )

    # solver.add(
    #     variable_unit == create_unit(
    #         create_scalar_instance(pair=scalar),
    #         *UNIT_TO_BASE_UNIT_VECTOR[description['SemanticInfo']['Units'][0]],
    #         *([0] * MAX_FUNCTION_PARAMETERS),
    #     ),
    #     variable_frames == Frames.frames(
    #         *([id in description_frames for id in range(NUM_FRAMES)]),
    #     )
    # )
    assert_and_check(
        variable_unit == create_unit(
            create_scalar_instance(pair=scalar),
            *UNIT_TO_BASE_UNIT_VECTOR[description['SemanticInfo']['Units'][0]],
            *([0] * MAX_FUNCTION_PARAMETERS),
        ),
        f'{name} unit known from prior type file',
    )
    assert_and_check(
        variable_frames == Frames.frames(
            *([id in description_frames for id in range(NUM_FRAMES)]),
        ),
        f'{name} frame known from prior type file',
    )
    assert_and_check(
        variable_type == Type.type(
            variable_unit,
            variable_frames,
        ),
        f'{name} known from prior type file',
    )


def load_message_definitions(io: TextIO):
    data = io.read()
    xml = ET.fromstring(data)
    if xml.tag == 'MDM':
        parse_cmasi(xml)
    elif xml.tag == 'mavlink':
        parse_mavlink(xml)
    else:
        raise ValueError('Unsupported definition file')


def parse_cmasi(xml: ET.ElementTree):
    for elt in xml.findall('*/Struct'):
        struct_name = elt.attrib['Name']
        for field in elt:
            unit_name = field.attrib.get('Units')
            if unit_name not in UNIT_TO_BASE_UNIT_VECTOR:
                log(
                    LogLevel.WARNING,
                    f'unrecognized unit: {unit_name}. Skipping.',
                )
                continue
            field_name = field.attrib['Name'][0].upper(
            ) + field.attrib['Name'][1:]
            getter_name = f'afrl::cmasi::{struct_name}::get{field_name}'
            return_unit = Const(f'{getter_name}_units', Unit)
            return_frames = Const(f'{getter_name}_frames', Frames)
            return_type = Const(f'{getter_name}_return_type', Type)
            _fn_name_to_return_type[f'{getter_name}_return_type'] = return_type
            scalar = UNIT_TO_SCALAR[unit_name]
            # solver.add(
            #     return_unit == create_unit(
            #         create_scalar_instance(pair=scalar),
            #         *UNIT_TO_BASE_UNIT_VECTOR[unit_name],
            #         *([0] * MAX_FUNCTION_PARAMETERS),
            #     ),
            # )
            solver.assert_and_track(
                return_unit == create_unit(
                    create_scalar_instance(pair=scalar),
                    *UNIT_TO_BASE_UNIT_VECTOR[unit_name],
                    *([0] * MAX_FUNCTION_PARAMETERS),
                ),
                f'{getter_name} return unit known from CMASI definition',
            )
            solver.assert_and_track(
                return_type == Type.type(
                    return_unit,
                    return_frames,
                ),
                f'{getter_name} known from CMASI definition',
            )


def parse_mavlink(xml: ET.ElementTree):
    global _member_access_with_prior_types
    for message in xml.findall('*/message'):
        typename = f'mavlink_{message.attrib["name"].lower()}_t'
        for field in message.findall('field'):
            unit_name = field.attrib.get('units')
            if not unit_name:
                continue
            elif unit_name not in UNIT_TO_BASE_UNIT_VECTOR:
                log(
                    LogLevel.WARNING,
                    f'unrecognized unit: {unit_name}',
                )
                continue

            _typename_has_unit[typename] = True
            scalar = UNIT_TO_SCALAR[unit_name]
            _member_access_with_prior_types.add(
                f'{typename}.{field.attrib["name"]}',
            )

            _member_access_to_type[f'{typename}.{field.attrib["name"]}'] = Type.type(
                create_unit(
                    create_scalar_instance(pair=scalar),
                    *UNIT_TO_BASE_UNIT_VECTOR[unit_name],
                    *([0] * MAX_FUNCTION_PARAMETERS),
                ),
                Frames.frames(*[True for _ in range(NUM_FRAMES)]),
            )


if __name__ == '__main__':
    main()