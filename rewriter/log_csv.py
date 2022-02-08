#!/usr/bin/env python3
from sys import argv, exit, stderr, stdout
from typing import Any, Dict, List, IO, Set, Sequence, Tuple

# Reads CSV into a dictionary relating timestamps to a map of variable ID -> value.
# Returns that dictionary, and a dictionary relating variable IDs to their first value.
def read_into_dict(handle: IO) -> Tuple[Dict[int, Dict[Any, float]], Dict[Any, float]]:
    result = {}
    first_values = {}
    is_first_line = True
    for line in handle:
        if is_first_line:
            is_first_line = False
            continue
        [variable_id_str, timestamp_str, val_str] = line.split(',')
        try:
            variable_id = int(variable_id_str)
        except Exception:
            variable_id = variable_id_str
        timestamp = int(timestamp_str)
        value = float(val_str)
        if first_values.get(variable_id, None) is None:
            first_values[variable_id] = value
        if vars_at_timestamp := result.get(timestamp):
            vars_at_timestamp[variable_id] = value
        else:
            result[timestamp] = {variable_id: value}
    return (result, first_values)

def read_variable_names(handle: IO) -> Dict[int, str]:
    result = {}
    handle.readline()
    for line in handle:
        variable_name, variable_id = line.split(',')
        result[int(variable_id)] = variable_name
    return result

def max_timestamp(d: Dict[int, Dict[Any, float]]) -> int:
    return max(d.keys())

def min_timestamp(d: Dict[int, Dict[Any, float]]) -> int:
    return min(d.keys())

def all_var_ids(d: Dict[int, Dict[Any, float]]) -> Set[Any]:
    result = set()
    for val in d.values():
        for id in val.keys():
            result.add(id)
    return result


def output_dict_to_csv(d: Dict[Any, List[Tuple[int, float]]], first_values: Dict[Any, float], variable_names: Dict[int, str], out: IO):
    min_time = min_timestamp(d)
    max_time = max_timestamp(d)
    sep = ''
    var_ids = all_var_ids(d)
    for var_id in var_ids:
        if type(var_id) is int:
            name = variable_names.get(var_id, f'v_{var_id}')
        else:
            name = var_id
        print(f'{sep}{name}', end='', file=out)
        sep = ','
    print('', file=out)
    last_values = {}
    for t in range(min_time, max_time+1):
        sep = ''
        for var_id in var_ids:
            #last_value = last_values.get(var_id, first_values[var_id])
            last_value = -42
            next_value = d.get(t, {}).get(var_id, last_value)
            last_values[var_id] = next_value
            print(f'{sep}{next_value}', end='', file=out)
            sep = ','
        print('', file=out)


def output_dict_to_csv_v2(d: Dict[int, List[Tuple[int, float]]], variable_names: Dict[Any, str], out: IO, unroll=10):
    min_time = min_timestamp(d)
    max_time = max_timestamp(d)
    sep = ''
    var_ids = all_var_ids(d)
    for var_id in var_ids:
        if type(var_id) is int:
            default_name = f'v_{var_id}'
        else:
            default_name = var_id
        name = variable_names.get(var_id, default_name)
        print(f'{sep}{name}', end='')
        sep = ','
    print('', file=out)
    last_values = {}
    for t in range(min_time, max_time + 1):
        sep = ''
        for var_id in var_ids:
            last_value = last_values.get(var_id, 0.0)
            next_value = d.get(t, {}).get(var_id, last_value)
            last_values[var_id] = last_value
            print(f'{sep}{next_value}', end='', file=out)
            sep = ','
        print('', file=out)

def main(argv: Sequence[str]):
    if len(argv) != 2:
        print(f'Usage: {argv[0]} [path to log file]', file=stderr)
        exit(1)
    variable_names = None
    with open('variable_names.csv') as fd:
        variable_names = read_variable_names(fd)
    with open(argv[1]) as fd:
        d, first_values = read_into_dict(fd)
        output_dict_to_csv(d, first_values, variable_names, stdout)
        #output_dict_to_csv_v2(d, variable_names, stdout)

if __name__ == '__main__':
    main(argv)
