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


def get_stored_stu(compile_command: cindex.CompileCommand, cache_path: Optional[str]) -> Optional[SerializedTU]:
    '''Retieves and returns a stored serialized translation unit or returns None'''
    # Get the time the file was last modified
    full_path = os.path.join(
        compile_command.directory,
        compile_command.filename,
    )
    modified_time = os.path.getmtime(full_path)
    # Check for a stored serialized Translation Unit in memory and compare last modified times
    cache_key = _translation_unit_file_path_to_filename(full_path)
    if cache_key in _tu_filename_to_stu:
        if _tu_filename_to_stu[cache_key].serialization_time >= modified_time:
            logger.info(f'Using in-memory cache for {cache_key}')
            return _tu_filename_to_stu[cache_key]
        else :
            logger.info(f'Dirty in-memory cache for {cache_key}')
    # Check for a stored serialized Translation Unit stored on the Hard Drive and compare last modified times
    else:
        logger.info(f'No in-memory cache for {cache_key}')
        if cache_path:
            serialized_tu = read_tu(
                cache_path,
                full_path,
            )
            if serialized_tu.serialization_time >= modified_time:
                logger.info(f'Using cached analysis for {full_path}')
                return serialized_tu


def read_tu(path: str, file_path: str) -> SerializedTU:
    '''Loads a serialized Transition unit stored in a file.'''
    cache_key = _translation_unit_file_path_to_filename(file_path)
    tu_pathname = os.path.join(path, cache_key + '.json')
    try:
        with open(tu_pathname) as f:
            data = json.load(f)
            stu = SerializedTU(
                data['SerializationTime'],
                data['Assertions'],
                data['Solver'],
                file_path,
            )
            save_stu_to_memory(stu)
            return stu
    except Exception:
        return SerializedTU(0, [], [], file_path)


def save_stu_to_memory(stu: SerializedTU) -> None:
    '''Saves a serialized Transition Unit into the in memory cache.'''
    cache_key = _translation_unit_file_path_to_filename(stu.spelling)
    logger.info(f"Writing to in-memory cache {cache_key}")
    _tu_filename_to_stu[cache_key] = stu


def parse_tu(compile_command: cindex.CompileCommand, cache_path: Optional[str] = None) -> Optional[Union[cindex.TranslationUnit, SerializedTU]]:
    '''Parses the translation unit.'''
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
            logger.warning(f'Parsing: {compile_command.filename}: {diag}')
        return translation_unit
    except cindex.TranslationUnitLoadError:
        logger.warning(
            f'could not parse {os.path.join(compile_command.directory, compile_command.filename)}',
        )
        return None


def serialize_tu(tu: cindex.TranslationUnit, tu_solver: z3.Solver, tu_assertions: List[z3.BoolRef]) -> SerializedTU:
    '''Returns a serialized Translation Unit'''
    return SerializedTU(
        int(time.time()),
        [str(a) for a in tu_assertions],
        tu_solver.to_smt2(),
        tu.spelling,
    )


def write_tu(path: str, stu: 'SerializedTU'):
    '''Saves a Serialized Translation Unit to a file.'''
    tu_pathname = os.path.join(
        path, _translation_unit_file_path_to_filename(stu.spelling) + '.json')
    with open(tu_pathname, 'w') as f:
        serialized_obj = {
            'SerializationTime': stu.serialization_time,
            'Assertions': stu.assertions,
            'Solver': stu.solver,
        }
        json.dump(serialized_obj, f)
        
        


def _translation_unit_file_path_to_filename(tuSpelling: str) -> str:
    return tuSpelling.replace('/', '_')
