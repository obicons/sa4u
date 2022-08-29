import asyncio
import aiohttp
import argparse
import clang.cindex as cindex
import flex
import json
import logging
import os.path
import protocol_definitions
import time
import xml.etree.ElementTree as ET
import signal
import threading
from tu import *
from typing import Any, Dict, Optional, Set, TextIO, Tuple
from util import *
from z3 import *
import multiprocessing

logger = logging.getLogger()

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
    'UNIX': 22,
}

# The number of frames there are.
NUM_FRAMES = 23  # len(list(MAV_FRAME_TO_ID.keys()))

# The maximum number of parameters one of our functions can accept.
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
    'meter/sec': [1, -1, 0, 0, 0, 0, 0],
    'meter/sec/sec': [1, -2, 0, 0, 0, 0, 0],
    'millisecond': [0, 1, 0, 0, 0, 0, 0],
    'milliseconds': [0, 1, 0, 0, 0, 0, 0],
    'mm': [1, 0, 0, 0, 0, 0, 0],
    'ms': [0, 1, 0, 0, 0, 0, 0],
    'm/s': [1, -1, 0, 0, 0, 0, 0],
    'm/s/s': [1, -2, 0, 0, 0, 0, 0],
    's': [0, 1, 0, 0, 0, 0, 0],
    'sec': [0, 1, 0, 0, 0, 0, 0],
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
    'meter/sec': (1, 1),
    'meter/sec/sec': (1, 1),
    'millisecond': (1, 1000),
    'milliseconds': (1, 1000),
    'm': (1, 1),
    'mgauss': (1, 10000000),
    'mm': (1, 1000),
    'ms': (1, 1000),
    'm/s': (1, 1),
    'm/s/s': (1, 1),
    's': (1, 1),
    'sec': (1, 1),
    'us': (1, 1000000),
}

# Stores the return type of functions.
_fn_name_to_return_type: Dict[str, DatatypeRef] = {}

# Stores the types of variables.
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

# Global variable: the current TU's assert_and_check statements.
tu_solver: Solver

# Global variable: all the booleans that need assumed in this TU.
tu_assertions: List[BoolRef] = []

# Global variable: the Z3 solving context.
solver: Solver

# Stores a counter for creating unique messages and labeling expressions.
_counter = 0

# Stores if we should use a power-of-ten representation.
_use_power_of_ten: bool = False

# Stores if we should use scalar prefixes.
_enable_scalar_prefixes: bool = True

# Stores member access strings with prior known types.
_member_access_with_prior_types: Set[str] = set()

# Stores FQNs of frame accesses;
_member_frame_accesses: Set[str] = set()

# Functions to ignore.
_IGNORE_FUNCS = {
    'AP_Logger_Backend::Write_Message',
    'AP_Proximity_Backend::database_push',
    'AP_Proximity_Backend::ignore_reading',
    'calloc',
    '::::_MAV_RETURN_uint8_t',
    '::::_MAV_RETURN_uint16_t',
    '::::_MAV_RETURN_uint32_t',
    '::::_MAV_RETURN_uint64_t',
    'malloc',
    '::::mav_array_memcpy',
    '::::::memcpy',
    'operator[]',
    'printf',
    'puts',
    '::px4_usleep',
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

# Ignore these source directories.
_IGNORE_DIRS = {
    '.',
    'conversion',
    'matrix',
    'v2.0',
}

# ensure only one run can be in queue at a time
_run_lock = threading.BoundedSemaphore(1)


def main():
    global _enable_scalar_prefixes, _use_power_of_ten, tu_assertions, tu_solver, solver, _run_lock

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
        required=False,
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
        '-d',
        '--run-as-daemon',
        dest='run_as_daemon',
        help='run as daemon',
        required=False,
        type=bool,
        default=False,
    )
    parser.add_argument(
        '--power-of-10',
        action=argparse.BooleanOptionalAction,
        dest='power_of_ten',
        help='use a power of 10 representation of unit scalars',
        required=False,
        type=bool,
        default=False,
    )
    parser.add_argument(
        '-i',
        '--ignore-files',
        dest='ignore_files',
        help='files that sa4u does not need to analyze',
        action='append',
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        '--disable-scalar-prefixes',
        action=argparse.BooleanOptionalAction,
        dest='disable_scalar_prefixes',
        help='do not use scalar prefixes. can speed up analysis.',
        required=False,
        type=bool,
        default=False,
    )
    parser.add_argument(
        '--serialize-analysis',
        dest='serialize_analysis_path',
        help='path to save analysis results to',
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        '--flex-module-api-url',
        dest='flex_module_api_url',
        help='URL to communicate with flex module API',
        required=False,
        type=str,
        default=None,
    )
    parsed_args = parser.parse_args()

    protocol_definition_location = parsed_args.flex_module_api_url or parsed_args.message_definition_path
    if not protocol_definition_location:
        print(
            'Error: either --message-definition or --flex-module-api-url must be supplied.',
            file=sys.stderr,
        )
        parser.print_help(sys.stderr)
        exit(1)
    protocol_definition_src = protocol_definitions.ProtocolDefinitionSource.from_location(
        protocol_definition_location,
    )

    logging.basicConfig(
        level=logging.DEBUG,
        format='%(levelname)s: %(message)s',
    )

    signal.signal(signal.SIGHUP, HUP_signal_handler)
    signal.signal(signal.SIGTERM, TERM_signal_handler)

    _use_power_of_ten = parsed_args.power_of_ten
    _enable_scalar_prefixes = not parsed_args.disable_scalar_prefixes
    ignore_files = set(parsed_args.ignore_files or [])

    while True:
        _run_lock.acquire()
        print("---Started---", flush=True)

        initialize_z3()
        tu_solver = solver

        with open(parsed_args.prior_types_path, 'r') as prior_types_fd:
            load_prior_types(prior_types_fd)

        load_message_definitions(protocol_definition_src)

        compilation_database: cindex.CompilationDatabase = cindex.CompilationDatabase.fromDirectory(
            parsed_args.compilation_database_path,
        )

        analysis_dir: Optional[str] = parsed_args.serialize_analysis_path
        ensure_analysis_dir(analysis_dir)
        all_assertions = tu_assertions

        start = time.time()

        cindex_dict: Dict[str, cindex.CompileCommand] = filename_to_compile_cmd(
            parsed_args.compilation_database_path,
        )

        _NUM_PROCESSES = multiprocessing.cpu_count()
        inputQueue: multiprocessing.Queue[Optional[cindex.CompileCommand]] = multiprocessing.Queue(
            len(cindex_dict),
        )
        outputQueue: multiprocessing.Queue[Optional[SerializedTU]] = multiprocessing.Queue(
            _NUM_PROCESSES,
        )
        processes: multiprocessing.Process = []

        for i in range(_NUM_PROCESSES):
            process = multiprocessing.Process(
                target=child_walkers,
                args=(inputQueue, outputQueue, parsed_args.compilation_database_path,
                      analysis_dir),
            )
            process.start()
            processes.append(process)

        for cmd in cindex_dict:
            if os.path.basename(cindex_dict[cmd].filename) in ignore_files:
                logger.info(
                    f'Skipping {cindex_dict[cmd].filename} in {cindex_dict[cmd].directory} because it is to be ignored',
                )
                continue

            stu = get_stored_stu(cindex_dict[cmd], analysis_dir)
            if isinstance(stu, SerializedTU):
                all_assertions += get_z3_assertions_from_stu(stu)
            else:
                inputQueue.put(os.path.join(cmd))

        for i in range(len(processes)):
            inputQueue.put(None)

        count: int = 0
        while count != _NUM_PROCESSES:
            output = outputQueue.get()
            if output is None:
                count += 1
                continue
            save_stu_to_memory(output)
            all_assertions += get_z3_assertions_from_stu(output)

        for process in processes:
            process.join()

        end = time.time()
        print(f'Parsing elapsed time: {end - start} seconds', flush=True)

        # with open('smt_out', 'w') as fd:
        #     print(solver.to_smt2(), file=fd)
        #     exit(1)

        # for assertion in solver.assertions():
        #     print(assertion)

        start = time.time()
        solver.set(timeout=5 * 60 * 1000)
        status = solver.check(all_assertions)
        end = time.time()
        print(f'Z3 elapsed time: {end - start} seconds', flush=True)
        if status != sat:
            print('ERROR!')
            core = solver.unsat_core()
            for failure in core:
                print(f'  {failure}')
        # elif status == sat:
        #    print('===MODEL===')
        #    for m in solver.model():
        #        print(f'{m} = {solver.model()[m]}')
        #    print(f'Ignored {_ignored} of {_num_exprs}')
        if not parsed_args.run_as_daemon:
            break
        print(f'---END RUN---', flush=True)


_ignored = 0
_num_exprs = 0


def HUP_signal_handler(sig_num: int, _frame):
    global _run_lock
    try:
        _run_lock.release()
    except ValueError:
        pass
    print("In HUP signal handler...", flush=True)


def TERM_signal_handler(sig_num: int, _frame):
    sys.exit()


def get_z3_assertions_from_stu(tu: SerializedTU) -> List[BoolRef]:
    global solver
    tmp_solver = Solver()
    tmp_solver.from_string(tu.solver)
    solver.add(tmp_solver.assertions())
    return [Const(s, BoolSort())
            for s in tu.assertions]


def child_walkers(input: multiprocessing.Queue, output: multiprocessing.Queue, compilation_database_path: str, analysis_dir: Optional[str] = None) -> None:
    global tu_assertions, tu_solver
    initialize_z3()
    tu_solver = solver
    cindex_dict = filename_to_compile_cmd(compilation_database_path)

    path: str
    for path in iter(input.get, None):
        compile_command = cindex_dict[path]
        tu = parse_tu(compile_command, analysis_dir)
        if tu is None:
            continue

        if isinstance(tu, cindex.TranslationUnit):
            ignore_locations = get_ignore_lines(tu)

            # tu is always a TranslationUnit, since this runs in a new process.
            cursor: cindex.Cursor = tu.cursor
            tu_solver = Solver()
            tu_assertions = []
            walk_ast(
                cursor,
                walker,
                {
                    'Seen': set([]),
                    'IgnoreLocations': ignore_locations,
                },
            )

            stu = serialize_tu(tu, tu_solver, tu_assertions)
            if analysis_dir:
                write_tu(analysis_dir, stu)
        else:
            stu = tu

        output.put(stu)
    output.put(None)


def filename_to_compile_cmd(compilation_database_path: str,) -> Dict[str, cindex.CompileCommand]:
    compilation_database: cindex.CompilationDatabase = cindex.CompilationDatabase.fromDirectory(
        compilation_database_path,
    )
    cindex_dict: Dict[str, cindex.CompileCommand] = {}
    for cmd in compilation_database.getAllCompileCommands():
        cindex_dict[os.path.join(cmd.directory, cmd.filename)] = cmd
    return cindex_dict


def walker(cursor: cindex.Cursor, data: Dict[Any, Any]) -> WalkResult:
    global _counter, _ignored
    filename: str = ''
    if _ignore_cursor(cursor, data['IgnoreLocations']):
        return WalkResult.CONTINUE

    if cursor.location.file is not None:
        home = os.getenv('HOME') or ''
        filename = cursor.location.file.name
        if not filename.startswith(home) and not filename.startswith('/src/'):
            return WalkResult.CONTINUE

        dirname = os.path.basename(os.path.dirname(filename))
        if dirname in _IGNORE_DIRS:
            return WalkResult.CONTINUE

    cursor_descr = f'{filename}_{cursor.location.line}_{cursor.location.column}_{cursor.get_usr()}'
    if cursor_descr in data['Seen']:
        return WalkResult.CONTINUE
    data['Seen'].add(cursor_descr)

    if cursor.kind == cindex.CursorKind.FUNCTION_DECL or cursor.kind == cindex.CursorKind.CXX_METHOD:
        data['CurrentFn'] = get_fq_name(cursor)
        data['ActiveConstraints'] = {}
        data['NextId'] = 0
        data['ParamNamesToId'] = {}
        logger.debug(f'IN {data["CurrentFn"]}')
        return WalkResult.RECURSE
    elif cursor.kind == cindex.CursorKind.PARM_DECL:
        if data.get('ParamNamesToId') is None:
            data['ParamNamesToId'] = {}
            data['NextId'] = 0
        data['ParamNamesToId'][cursor.spelling] = data['NextId']
        data['NextId'] += 1
        return WalkResult.CONTINUE
    elif cursor.kind == cindex.CursorKind.VAR_DECL:
        if 'CurrentFn' in data:
            logger.debug(f'DECL IN {data["CurrentFn"]}')

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
        if get_lhs(cursor).spelling == 'operator[]':
            _ignored += 1
            return WalkResult.CONTINUE

        lhs_type = type_expr(get_lhs(cursor), data)
        if lhs_type is None:
            logger.waring(
                f'unrecognized lhs type @ {cursor.location.file} line {cursor.location.line}',
            )
            _ignored += 1
            return WalkResult.CONTINUE

        rhs_type = type_expr(get_rhs(cursor), data)
        if rhs_type is None:
            _ignored += 1
            logger.warning(
                f'unrecognized rhs type @ {cursor.location.file} line {cursor.location.line}',
            )
            return WalkResult.CONTINUE

        assert_and_check(
            Or(
                # lhs_type == rhs_type,
                types_equal(lhs_type, rhs_type),
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
            logger.warning(f'Skipping function {fq_fn_name}')
            _ignored += 1
            return WalkResult.CONTINUE

        func_name = String(fq_fn_name)
        for arg, arg_no in zip(get_arguments(cursor), range(0, 1000)):
            if arg is None:
                _ignored += 1
                logger.warning(
                    f'no argument cursor found in {cursor.location.file} on line {cursor.location.line}',
                )
                continue

            arg_type = type_expr(arg, data)
            if arg_type is None:
                _ignored += 1
                logger.warning(
                    f'unknown argument type in {cursor.location.file} on line {cursor.location.line}',
                )
                return WalkResult.RECURSE
            assert_and_check(
                type_expr(arg, data) == ArgType(func_name, arg_no),
                # types_equal(type_expr(arg, data), ArgType(func_name, arg_no)),
                f'Call to {func_name.as_string()} in {cursor.location.file} on line {cursor.location.line} column {cursor.location.column} ({_counter})',
            )
            _counter += 1
        return WalkResult.RECURSE
    elif cursor.kind == cindex.CursorKind.IF_STMT:
        constraint = extract_conditional_constraints(cursor)
        if constraint is None:
            return WalkResult.RECURSE

        data['HadConstraint'] = True
        data['ActiveConstraints'][constraint[0]] = constraint[1]
        walk_ast(cursor, walker, data)

        if has_return_statement(cursor):
            data['ActiveConstraints'][
                constraint[0]
            ] = invert_frame(constraint[1])
        else:
            del data['ActiveConstraints'][constraint[0]]

        return WalkResult.CONTINUE
    return WalkResult.RECURSE


def kind_printer(cursor: cindex.Cursor, _) -> WalkResult:
    print(f'kind: {cursor.kind} {cursor.spelling}')
    return WalkResult.RECURSE


def type_expr(cursor: cindex.Cursor, context: Dict[Any, Any]) -> Optional[DatatypeRef]:
    global _counter, _ignored, _num_exprs
    _num_exprs += 1
    if cursor.kind == cindex.CursorKind.CALL_EXPR:
        if cursor.referenced is None:
            logger.warning(
                f'unknown call to {cursor.spelling} in {cursor.location.file} on line {cursor.location.line} column {cursor.location.column}',
            )
            return None

        fq_fn_name = get_fq_name(cursor.referenced)
        if fq_fn_name in _IGNORE_FUNCS:
            _ignored += 1
            return None

        reference_typename = f'{get_fq_name(cursor.referenced)}_return_type'
        if _fn_name_to_return_type.get(reference_typename) is None:
            t = Const(
                reference_typename,
                Type,
            )
            _fn_name_to_return_type[reference_typename] = t
            assert_and_check(
                Type.is_constant(t) == False,
                'return type is not a constant',
            )

        func_name = String(fq_fn_name)
        for arg, arg_no in zip(get_arguments(cursor), range(0, 1000)):
            if arg is None:
                _ignored += 1
                logger.warning(
                    f'no argument cursor found in {cursor.location.file} on line {cursor.location.line}',
                )
                continue

            arg_type = type_expr(arg, context)
            if arg_type is None:
                _ignored += 1
                logger.warning(
                    f'unknown argument type in {cursor.location.file} on line {cursor.location.line}',
                )
                break
            assert_and_check(
                type_expr(arg, context) == ArgType(func_name, arg_no),
                # types_equal(type_expr(arg, data), ArgType(func_name, arg_no)),
                f'Call to {func_name.as_string()} in {cursor.location.file} on line {cursor.location.line} column {cursor.location.column} ({_counter})',
            )

        return _fn_name_to_return_type.get(reference_typename)
    elif cursor.kind == cindex.CursorKind.VARIABLE_REF or cursor.kind == cindex.CursorKind.DECL_REF_EXPR:
        if context.get('CurrentFn') and cursor.spelling in context.get('ParamNamesToId', {}):
            return ArgType(
                String(context['CurrentFn']),
                context['ParamNamesToId'][cursor.spelling],
            )
        if cursor.referenced is None:
            return None

        var_typename = f'{get_fq_name(cursor.referenced)}_type'
        if _var_name_to_type.get(var_typename) is None:
            _var_name_to_type[var_typename] = Const(var_typename, Type)
        return _var_name_to_type[var_typename]
    elif cursor.kind == cindex.CursorKind.UNEXPOSED_EXPR:
        logger.warning(
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
                logger.warning(
                    f'untyped expression @ {loc.file} line {loc.line}',
                )
                return None
            assert_and_check(
                Or(
                    lhs_type == rhs_type,
                    And(
                        is_dimensionless(lhs_type),
                        is_dimensionless(rhs_type),
                    ),
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
                logger.warning(
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
                And(
                    Type.is_constant(lhs_type),
                    Type.is_constant(rhs_type),
                ),
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
            # FreshConst(Unit, 'literal_unit'),
            FreshConst(Frames, 'literal_frames'),
            True,
        )
        return literal_type
    elif cursor.kind == cindex.CursorKind.FLOATING_LITERAL:
        return Type.type(
            create_unit(
                create_scalar_instance(num=get_floating_literal(cursor)),
                *UNIT_TO_BASE_UNIT_VECTOR['literal'],
                *([0] * MAX_FUNCTION_PARAMETERS),
            ),
            # FreshConst(Unit, 'literal_unit'),
            FreshConst(Frames, 'literal_frames'),
            True,
        )
    elif cursor.kind == cindex.CursorKind.MEMBER_REF_EXPR or cursor.kind == cindex.CursorKind.ARRAY_SUBSCRIPT_EXPR:
        frame_constraint = None
        if cursor.kind == cindex.CursorKind.MEMBER_REF_EXPR:
            accessed_object = get_next_decl_ref_expr(cursor)
            if accessed_object is not None:
                obj_name = get_fq_name(accessed_object)
                frame_constraint = context['ActiveConstraints'].get(obj_name)

        expr_repr = get_fq_member_expr(cursor)
        if expr_repr in _IGNORE_MEMBERS:
            _ignored += 1
            return None

        for access in _member_frame_accesses:
            typename = access.split('.')[0]
            expr_repr_type = expr_repr.split('.')[0]
            if typename == expr_repr_type and context['ActiveConstraints'] == {}:
                logger.error(
                    f'No constraints active for member access @ {cursor.location.file} line {cursor.location.line}',
                )

        t = _member_access_to_type.get(expr_repr)
        if t is None:
            t = FreshConst(Type, 'member accessed')
            _member_access_to_type[
                get_fq_member_expr(cursor)
            ] = t

        if frame_constraint is not None:
            return Type.type(
                Type.unit(t),
                frame_constraint,
                False,
            )
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


def extract_conditional_constraints(if_stmt: cindex.Cursor) -> Optional[Tuple[str, DatatypeRef]]:
    body_expr = get_lhs(if_stmt)
    operator = get_binary_op(body_expr)
    if operator == '==' or operator == '!=':
        constrained_object = maybe_get_constrained_object(
            body_expr,
            _member_frame_accesses,
        )
        if constrained_object is None:
            return None

        constraint_literal = maybe_get_constraint_literal(body_expr)
        if constraint_literal is None:
            return None

        if constraint_literal > NUM_FRAMES:
            logger.warning(f'Unrecognized frame: {constraint_literal}')
            return None

        if operator == '==':
            framev = [False] * NUM_FRAMES
            framev[constraint_literal] = True
        elif operator == '!=':
            framev = [True] * NUM_FRAMES
            framev[constraint_literal] = False

        frame = Frames.frames(framev)

        return (constrained_object, frame)
    return None


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
            'No arguments provided to create_scalar_instance().',
        )


def invert_frame(frame: DatatypeRef) -> DatatypeRef:
    global _counter
    f = FreshConst(Frames, 'inverted_frame')
    assert_and_check(
        And(
            *[Frames.__dict__[f'frame_{i}'](f) != Frames.__dict__[f'frame_{i}'](frame)
              for i in range(NUM_FRAMES)],
        ),
        f'frame inverted {_counter}',
    )
    _counter += 1
    # solver.add(
    #     And(
    #         *[Frames.__dict__[f'frame_{i}'](f) != Frames.__dict__[f'frame_{i}'](frame)
    #           for i in range(NUM_FRAMES)],
    #     ),
    # )
    return f


def declare_types():
    global Frames, Rational, Type, TypeOf, Unit

    scalar_dt: DatatypeSortRef
    if _use_power_of_ten:
        # scalar_dt = RealSort()
        scalar_dt = IntSort()
        Rational = IntSort()
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
        ('is_constant', BoolSort()),
    )
    Type = Type.create()


def types_equal(t1: DatatypeRef, t2: DatatypeRef) -> BoolRef:
    '''Returns if the 2 types are equal (ignoring is_constant).'''
    return Or(
        And(
            Type.unit(t1) == Type.unit(t2),
            Type.frame(t1) == Type.frame(t2),
        ),
        Type.is_constant(t1),
        Type.is_constant(t2),
    )


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
    b = Const(msg, BoolSort())
    tu_solver.add(Implies(b, stmt))
    tu_assertions.append(b)


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
    var_type = Type.type(
        variable_unit,
        variable_frames,
        False,
    )
    assert_and_check(
        variable_type == var_type,
        f'{name} known from prior type file',
    )
    _member_access_to_type[name] = var_type
    # assert_and_check(
    #     types_equal(
    #         variable_type,
    #         Type.type(
    #             variable_unit,
    #             variable_frames,
    #             False,
    #         ),
    #     ),
    #     f'{name} known from prior type file',
    # )


def load_message_definitions(proto_src: protocol_definitions.ProtocolDefinitionSource):
    if proto_src.kind == protocol_definitions.ProtocolDefinitionSourceType.ProtocolFile:
        _load_from_file(proto_src.location)
    elif proto_src.kind == protocol_definitions.ProtocolDefinitionSourceType.FlexModuleAPI:
        loop = asyncio.get_event_loop()
        loop.run_until_complete(_load_from_flex_module_api(proto_src.location))
    else:
        raise ValueError(f'Unsupported protocol kind: {proto_src.kind}')


async def _load_from_flex_module_api(api_url: str):
    async with aiohttp.ClientSession() as session:
        api = flex.FlexAPI(api_url, session)

        # TODO (max): For now, we just load the LMCP messages.
        # We also need to load the MAVLink messages.
        lmcp_messages = await api.download_messages(flex.Package.OPENUXAS_LMCP_V3)
        futures = [
            api.download_struct_by_url(msg.url)
            for msg in lmcp_messages
        ]

        for struct_definition_future in asyncio.as_completed(futures):
            struct_definition = await struct_definition_future

            # TODO (max): Merge this code with the XML definition handling code.
            for field in struct_definition.fields:
                unit_name = field.unit_name
                if unit_name is None or unit_name.lower() == 'none':
                    continue
                elif unit_name not in UNIT_TO_BASE_UNIT_VECTOR:
                    logger.warning(
                        f'unrecognized unit: {unit_name}. Skipping.',
                    )
                    continue
                field_name = field.name[0].upper() + field.name[1:]
                getter_name = f'afrl::cmasi::{struct_definition.name}::get{field_name}'
                print(getter_name)
                return_unit = Const(f'{getter_name}_units', Unit)
                return_frames = Const(f'{getter_name}_frames', Frames)
                return_type = Const(f'{getter_name}_return_type', Type)
                _fn_name_to_return_type[f'{getter_name}_return_type'] = return_type
                scalar = UNIT_TO_SCALAR[unit_name]
                tu_solver.assert_and_track(
                    return_unit == create_unit(
                        create_scalar_instance(pair=scalar),
                        *UNIT_TO_BASE_UNIT_VECTOR[unit_name],
                        *([0] * MAX_FUNCTION_PARAMETERS),
                    ),
                    f'{getter_name} return unit known from CMASI definition',
                )
                tu_solver.assert_and_track(
                    return_type == Type.type(
                        return_unit,
                        return_frames,
                        False,
                    ),
                    f'{getter_name} known from CMASI definition',
                )


def _load_from_file(filename: str):
    with open(filename) as io:
        data = io.read()
        xml = ET.fromstring(data)
        if xml.tag == 'MDM':
            parse_cmasi(xml)
        elif xml.tag == 'mavlink':
            parse_mavlink(xml)
        else:
            raise ValueError('Unsupported definition file')


def parse_cmasi(xml: ET.Element):
    for elt in xml.findall('*/Struct'):
        struct_name = elt.attrib['Name']
        for field in elt:
            unit_name = field.attrib.get('Units')
            if unit_name is None or unit_name.lower() == 'none':
                continue
            if unit_name not in UNIT_TO_BASE_UNIT_VECTOR:
                logger.warning(f'unrecognized unit: {unit_name}. Skipping.')
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
            tu_solver.assert_and_track(
                return_unit == create_unit(
                    create_scalar_instance(pair=scalar),
                    *UNIT_TO_BASE_UNIT_VECTOR[unit_name],
                    *([0] * MAX_FUNCTION_PARAMETERS),
                ),
                f'{getter_name} return unit known from CMASI definition',
            )
            tu_solver.assert_and_track(
                return_type == Type.type(
                    return_unit,
                    return_frames,
                    False,
                ),
                f'{getter_name} known from CMASI definition',
            )


def parse_mavlink(xml: ET.Element):
    global _member_access_with_prior_types
    for message in xml.findall('*/message'):
        typename = f'mavlink_{message.attrib["name"].lower()}_t'
        for field in message.findall('field'):
            unit_name = field.attrib.get('units')
            if not unit_name:
                is_frame_field = field.attrib.get('enum') == 'MAV_FRAME'
                if is_frame_field:
                    _member_frame_accesses.add(
                        f'{typename}.{field.attrib["name"]}')
                continue
            elif unit_name not in UNIT_TO_BASE_UNIT_VECTOR:
                logger.warning(f'unrecognized unit: {unit_name}')
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
                False,
            )


def _ignore_cursor(cursor: cindex.Cursor, ignore_locations: List[Tuple[str, int]]) -> bool:
    if cursor.location is None:
        return False

    return any([l[0] == cursor.location.file.name and l[1] ==
                cursor.location.line for l in ignore_locations])


if __name__ == '__main__':
    main()
