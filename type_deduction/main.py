import asyncio
import mine
import multiprocessing
import pandas as pd
import sys
from sys import argv
from typing import Sequence, Tuple


async def main(argv: Sequence[str]):
    if len(argv) != 3:
        _usage(argv[0])
    data = pd.read_csv(argv[1])
    remove_constant_variables(data)
    remove_boolean_variables(data)
    kernel_variables = _read_variable_names(argv[2])

    # This line removes way too many invariants.
    # Maybe we explore this more later?
    # remove_write_once_variables(data)

    pool = multiprocessing.Pool(multiprocessing.cpu_count())
    tasks = column_numbers(len(data.columns), multiprocessing.cpu_count())
    # for i in range(1, 11):
    #     print(f'epsilon = {i * 0.1}', flush=True)
    #     res = sum(pool.starmap(
    #         mine.mine_equal_variables,
    #         [(data, start, end, 0.1 * i) for start, end in tasks],
    #     ))
    #     print(
    #         f'epsilon = {i * 0.1}, invariants = {res}',
    #         file=sys.stderr,
    #         flush=True,
    #     )
    # for i in range(1, 11):
    #     print(f'epsilon = {i * 0.1}', flush=True)
    #     res = sum(pool.starmap(
    #         mine.mine_equal_variables_kernel,
    #         [(data, kernel_variables, start, end, 0.1 * i) for start, end in tasks],
    #     ))
    #     print(
    #         f'epsilon = {i * 0.1}, invariants = {res}',
    #         file=sys.stderr,
    #         flush=True,
    #     )

    # for i in range(1, 11):
    #     print(f'epsilon = {i * 0.1}', flush=True)
    #     res = pool.starmap(
    #         mine.mine_eventually_equal_variables,
    #         [(data, start, end, 0.1 * i) for start, end in tasks],
    #     )
    #     print(f'confidence = {i * 0.1} => {sum(res)} invariants',
    #          file=sys.stderr, flush=True)
    # for i in range(1, 11):
    #     print(f'epsilon = {i * 0.1}', flush=True)
    #     res = pool.starmap(
    #         mine.mine_eventually_equal_kernel_variables,
    #         [(data, kernel_variables, start, end, 0.1 * i)
    #          for start, end in tasks],
    #     )
    #     print(f'confidence = {i * 0.1} => {sum(res)} invariants',
    #           file=sys.stderr, flush=True)

    # for i in range(1, 11):
    #     res = pool.starmap(
    #         mine.mine_linear_invariants,
    #         [(data, start, end, 0.1 * i) for start, end in tasks],
    #     )
    #     print(f'correlation >= {i * 0.1} => {sum(res)} invariants',
    #           file=sys.stderr, flush=True)
    for i in range(1, 11):
        res = pool.starmap(
            mine.mine_linear_kernel_invariants,
            [(data, kernel_variables, start, end, 0.1 * i)
             for start, end in tasks],
        )
        print(f'correlation >= {i * 0.1} => {sum(res)} invariants',
              file=sys.stderr, flush=True)
    # res = pool.starmap(
    #     mine.mine_linear_invariants,
    #     [(data, start, end, 0.95) for start, end in tasks],
    # )
    # print(f'correlation >= 0.95 => {sum(res)} invariants',
    #       file=sys.stderr, flush=True)
    # res = pool.starmap(
    #     mine.mine_linear_invariants,
    #     [(data, start, end, 0.975) for start, end in tasks],
    # )
    # print(f'correlation >= 0.975 => {sum(res)} invariants',
    #       file=sys.stderr, flush=True)


def remove_constant_variables(data: pd.DataFrame):
    """
    Removes all variables from data that are constant throughout execution.
    """
    for column in data.columns:
        if (data[column][0] == data[column]).all():
            data.drop(columns=column, inplace=True)


def remove_boolean_variables(data: pd.DataFrame):
    """
    Removes all variables from data that only contain boolean values.
    """
    for column in data.columns:
        if ((data[column] == 0) | (data[column] == 1)).all():
            data.drop(columns=column, inplace=True)


def remove_write_once_variables(data: pd.DataFrame):
    """
    Removes all variables from data that are only written once.
    """
    for column in data.columns:
        if len(set(data[column])) == 2:
            data.drop(columns=column, inplace=True)


def column_numbers(num_columns: int, num_processors: int) -> Sequence[Tuple[int, int]]:
    """
    Returns a sequence of (start, end) columns.
    """
    columns_per_processor = int(num_columns / num_processors)
    out = []
    for i in range(0, num_processors):
        start_column = i * columns_per_processor
        end_column = start_column + columns_per_processor
        if i == num_processors - 1:
            end_column = num_columns
        out.append((start_column, end_column))
    return out


def _usage(program_name: str):
    print(
        f'usage: {program_name} [trace CSV] [kernel variable list]',
        file=sys.stderr,
    )
    sys.exit(1)


def _read_variable_names(filename: str) -> Sequence[str]:
    names = []
    with open(filename) as fd:
        for name in fd:
            names.append(name.strip())
    return names


if __name__ == '__main__':
    asyncio.run(main(argv))
