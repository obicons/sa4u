import logging
import ccsyspath
import clang.cindex as cindex
import json
import multiprocessing.pool
import os
import queue
import time
import z3
from dataclasses import dataclass
from typing import Any, Dict, Iterator, List, Optional, Union

logger = logging.getLogger()

_tu_filename_to_stu: Dict[str, 'SerializedTU'] = {}

# Number of threads to concurrently read translation units.
_NUM_WORKERS = 8

# Maximum number of waiting TUs that need analyzed.
_MAX_WAITING_TUS = 128


@dataclass
class SerializedTU:
    serialization_time: int
    assertions: List[str]
    solver: List[Any]
    spelling: str


def translation_units(compile_commands: cindex.CompilationDatabase, cache_path: Optional[str]) -> Iterator[Union[cindex.TranslationUnit, SerializedTU]]:
    '''Returns an iterator over a translation unit for each file in the compilation database.'''
    with multiprocessing.pool.ThreadPool(processes=_NUM_WORKERS) as pool:
        compile_commands = list(compile_commands.getAllCompileCommands())
        q: queue.Queue[Union[cindex.TranslationUnit, SerializedTU]] = queue.Queue(
            maxsize=_MAX_WAITING_TUS,
        )

        pool.starmap_async(
            _mp_parse_tu,
            [(cmd, q, cache_path) for cmd in compile_commands],
        )

        for _ in enumerate(compile_commands):
            item = q.get()
            if isinstance(item, Exception):
                raise item
            elif item is not None:
                yield item

    # compile_command: cindex.CompileCommand
    # for compile_command in compile_commands.getAllCompileCommands():
    #     maybe_tu = _parse_tu(compile_command, cache_path)
    #     if maybe_tu:
    #         yield maybe_tu


def _mp_parse_tu(compile_command: cindex.CompileCommand, q: multiprocessing.Queue, cache_path: Optional[str]):
    '''Parse a translation unit, and place the parsed result in the queue.'''
    try:
        q.put(_parse_tu(compile_command, cache_path))
    except Exception as err:
        q.put(err)


def _parse_tu(compile_command: cindex.CompileCommand, cache_path: Optional[str] = None) -> Optional[Union[cindex.TranslationUnit, SerializedTU]]:
    '''Parses the translation unit.'''
    if cache_path:
        full_path = os.path.join(
            compile_command.directory,
            compile_command.filename,
        )
        serialized_tu = read_tu(
            cache_path,
            full_path,
        )
        modified_time = os.path.getmtime(full_path)
        if serialized_tu.serialization_time >= modified_time:
            logger.info(f'Using cached analysis for {full_path}')
            return serialized_tu

    logger.info(f'parsing {compile_command.filename}')
    try:
        if 'lua' in compile_command.filename:
            return None
        os.chdir(compile_command.directory)
        translation_unit = cindex.TranslationUnit.from_source(
            os.path.join(compile_command.directory,
                         compile_command.filename),
            args=[arg for arg in compile_command.arguments
                  if arg != compile_command.filename] + ['-I' + inc.decode() for inc in ccsyspath.system_include_paths('clang')],
        )
        for diag in translation_unit.diagnostics:
            logger.warn(f'Parsing: {compile_command.filename}: {diag}')
        return translation_unit
    except cindex.TranslationUnitLoadError:
        logger.warn(
            f'could not parse {os.path.join(compile_command.directory, compile_command.filename)}',
        )
        return None


def read_tu(path: str, file_path: str) -> SerializedTU:
    tu_pathname = os.path.join(path, file_path.replace('/', '_') + '.json')
    if tu_pathname in _tu_filename_to_stu:
        logger.info(f"Using in-memory cache for {tu_pathname}")
        return _tu_filename_to_stu[tu_pathname]
    else:
        logger.info(f"No in-memory cache for {tu_pathname}")

    try:
        with open(tu_pathname) as f:
            data = json.load(f)
            _tu_filename_to_stu[tu_pathname] = SerializedTU(
                data['SerializationTime'],
                data['Assertions'],
                data['Solver'],
                file_path,
            )
            return _tu_filename_to_stu[tu_pathname]
    except Exception:
        return SerializedTU(0, [], [], file_path)


def serialize_tu(path: str, tu: cindex.TranslationUnit, tu_solver: z3.Solver, tu_assertions: List[z3.BoolRef]):
    '''Saves the translation unit's solver to a file.'''
    tu_pathname = os.path.join(
        path, _translation_unit_to_filename(tu) + '.json')
    with open(tu_pathname, 'w') as f:
        serialized_obj = {
            'Assertions': [str(a) for a in tu_assertions],
            'SerializationTime': int(time.time()),
            'Solver': tu_solver.to_smt2(),
        }
        json.dump(serialized_obj, f)
        logger.info(f"Writing to in-memory cache {tu_pathname}")
        _tu_filename_to_stu[tu_pathname] = SerializedTU(
            serialized_obj['SerializationTime'],
            serialized_obj['Assertions'],
            serialized_obj['Solver'],
            tu.spelling,
        )


def _translation_unit_to_filename(tu: cindex.TranslationUnit) -> str:
    return tu.spelling.replace('/', '_')
