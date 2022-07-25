import ccsyspath
import clang.cindex as cindex
import json
import os
import time
import z3
from dataclasses import dataclass
from log import *
from typing import Any, Dict, Iterator, List, Optional, Union

_tu_filename_to_stu: Dict[str, 'SerializedTU'] = {}


@dataclass
class SerializedTU:
    serialization_time: int
    assertions: List[str]
    solver: List[Any]
    spelling: str


def translation_units(compile_commands: cindex.CompilationDatabase, cache_path: Optional[str]) -> Iterator[Union[cindex.TranslationUnit, SerializedTU]]:
    '''Returns an iterator over a translation unit for each file in the compilation database.'''
    compile_command: cindex.CompileCommand
    for compile_command in compile_commands.getAllCompileCommands():
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
                log(LogLevel.INFO, f'Using cached analysis for {full_path}')
                yield serialized_tu
                continue

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


def read_tu(path: str, file_path: str) -> SerializedTU:
    tu_pathname = os.path.join(path, file_path.replace('/', '_') + '.json')
    if tu_pathname in _tu_filename_to_stu:
        log(LogLevel.INFO, f"Using in-memory cache for {tu_pathname}")
        return _tu_filename_to_stu[tu_pathname]
    else:
        log(LogLevel.INFO, f"No in-memory cache for {tu_pathname}")

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
        log(LogLevel.INFO, f"Writing to in-memory cache {tu_pathname}")
        _tu_filename_to_stu[tu_pathname] = SerializedTU(
            serialized_obj['SerializationTime'],
            serialized_obj['Assertions'],
            serialized_obj['Solver'],
            tu.spelling,
        )


def _translation_unit_to_filename(tu: cindex.TranslationUnit) -> str:
    return tu.spelling.replace('/', '_')
