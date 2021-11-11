import math
import numpy as np
import pandas as pd
import scipy.stats
import sklearn.linear_model
import sklearn.metrics
import sys
from typing import Optional, Sequence, Set, Tuple

_MIN_COEFFICIENT = 0.00001
_MIN_SAMPLES = 10


def mine_equal_variables(data: pd.DataFrame,
                         start_column: int = 0,
                         end_column: Optional[int] = None,
                         relative_error: float = 0.01) -> int:
    """
    Outputs variables in data that are always within relative_error.

    Args:
        data: The dataframe containing variable values.
              Each row is a timestamp. Each column is a variable.
        relative_error: The relative error to tolerate to consider variables equal.
        start_column: The column to start processing from. Helps us break tasks into chunks.
        end_column: The last column to consider.
    """
    if not end_column:
        end_column = len(data.columns)
    num_mined = 0
    for variable_index, variable_name in enumerate(data.columns[start_column:end_column]):
        for other_variable_name in data.columns[variable_index+1+start_column:]:
            if (((data[variable_name] - data[other_variable_name]).abs()) <= (relative_error * data[variable_name]).abs()).all():
                print(
                    f'{variable_name} = {other_variable_name}',
                )
                num_mined += 1
    return num_mined


def mine_equal_variables_kernel(data: pd.DataFrame,
                                kernel_variables: Set[str],
                                start_column: int = 0,
                                end_column: Optional[int] = None,
                                relative_error: float = 0.01) -> int:
    """
    Outputs variables in data that are always within relative_error.

    Args:
        data: The dataframe containing variable values.
              Each row is a timestamp. Each column is a variable.
        kernel_variables: The set of core variables to mine invariants from.
        relative_error: The relative error to tolerate to consider variables equal.
        start_column: The column to start processing from. Helps us break tasks into chunks.
        end_column: The last column to consider.
    """
    if not end_column:
        end_column = len(data.columns)
    num_mined = 0
    for variable_index, variable_name in enumerate(data.columns[start_column:end_column]):
        if variable_name in kernel_variables:
            continue
        for other_variable_name in kernel_variables:
            if other_variable_name not in data.columns:
                continue
            indices = (data[variable_name] != -
                       42) & (data[other_variable_name] != -42)
            if (((data[variable_name][indices] - data[other_variable_name][indices]).abs()) <= (relative_error * data[variable_name][indices]).abs()).all() and len(data[variable_name][indices]) > 0:
                print(
                    f'{variable_name} = {other_variable_name}',
                    flush=True,
                )
                num_mined += 1
    return num_mined


def mine_eventually_equal_variables(data: pd.DataFrame,
                                    start_column: int = 0,
                                    end_column: Optional[int] = None,
                                    confidence_threshold: int = 1,
                                    relative_error: float = 0.01) -> int:
    """
    Outputs pairs (x, y) in data where if x = v, then later y = v,
    with confidence > confidence_threshold.

    Args:
        data: The dataframe containing variable values.
        start_column: The column to start processing from.
        end_column: The column to end processing.
        confidence_threshold: The confidence required to output an invariant.

    Returns:
        The number of invariants mined.
    """
    if not end_column:
        end_column = len(data.columns)
    invariants_mined = 0
    for variable_index, variable_name in enumerate(data.columns[start_column:end_column]):
        for other_variable_index, other_variable_name in enumerate(data.columns):
            # Skip pairs of variables like (somevar, somevar).
            if variable_name == other_variable_name:
                continue

            num_equal_later = 0
            col1 = data[variable_name].to_numpy()
            col2 = data[other_variable_name].to_numpy()

            col2_idx = 0
            for i in range(len(col1)):
                for j in range(col2_idx, len(col2)):
                    if abs(col2[j] - col1[i]) <= abs(relative_error * col1[i]):
                        num_equal_later += 1
                        col2_idx = j + 1
                        break

            confidence = num_equal_later / len(col1)
            if confidence >= confidence_threshold:
                print(
                    f'{variable_name} = c -> F {other_variable_name} = c',
                )
                invariants_mined += 1
    return invariants_mined


def mine_eventually_equal_kernel_variables(data: pd.DataFrame,
                                           kernel_variables: Set[str],
                                           start_column: int = 0,
                                           end_column: Optional[int] = None,
                                           confidence_threshold: int = 1,
                                           relative_error: float = 0.01) -> int:
    """
    Outputs pairs (x, y) in data where if x = v, then later y = v,
    with confidence > confidence_threshold.

    Args:
        data: The dataframe containing variable values.
        kernel_variables: The core measurements.
        start_column: The column to start processing from.
        end_column: The column to end processing.
        confidence_threshold: The confidence required to output an invariant.

    Returns:
        The number of invariants mined.
    """
    if not end_column:
        end_column = len(data.columns)
    invariants_mined = 0
    for variable_index, variable_name in enumerate(data.columns[start_column:end_column]):
        for other_variable_name in kernel_variables:
            if variable_name in kernel_variables or other_variable_name not in data.columns:
                continue

            num_equal_later = 0
            indicies = (data[variable_name] != -
                        42) & (data[other_variable_name] != -42)
            col1 = data[variable_name][indicies].to_numpy()
            col2 = data[other_variable_name][indicies].to_numpy()

            if len(col1) == 0 or len(col2) == 0:
                continue

            col2_idx = 0
            for i in range(len(col1)):
                for j in range(col2_idx, len(col2)):
                    if abs(col2[j] - col1[i]) <= abs(relative_error * col1[i]):
                        num_equal_later += 1
                        col2_idx = j + 1

            confidence = num_equal_later / len(col1)
            if confidence >= confidence_threshold:
                print(
                    f'{variable_name} = c -> F {other_variable_name} = c',
                    flush=True,
                )
                invariants_mined += 1

            num_equal_later = 0
            col2_idx = 0
            for i in range(len(col2)):
                for j in range(col2_idx, len(col1)):
                    if abs(col2[j] - col1[i]) <= abs(relative_error * col1[i]):
                        num_equal_later += 1
                        col2_idx = j + 1
            confidence = num_equal_later / len(col1)
            if confidence >= confidence_threshold:
                print(
                    f'{other_variable_name} = c -> F {variable_name} = c',
                    flush=True,
                )
                invariants_mined += 1

    return invariants_mined


def mine_linear_invariants(data: pd.DataFrame,
                           start_column: int = 0,
                           end_column: Optional[int] = None,
                           pearson_threshold: float = 0.5) -> int:
    """
    Mines invariants in the form y = a * x + b.

    Args:
        data: The variables to extract invariants from.
        start_column: The column to start from. Helps us split tasks.
        end_column: The column to end search on. Helps us split tasks.
        pearson_threshold: The absolute value threshold of the Pearson coefficient to reject invariants as uncorrelated. 
    """
    if end_column is None:
        end_column = len(data.columns)

    invariants_mined = 0
    for idx, variable_name in enumerate(data.columns[start_column:end_column]):
        y = data[variable_name]
        for other_variable in data.columns[start_column+idx+1:]:
            x = data[other_variable]
            pearson_correlation, _ = scipy.stats.pearsonr(x, y)
            if math.isnan(pearson_correlation) or abs(pearson_correlation) < pearson_threshold:
                continue

            regression = sklearn.linear_model.LinearRegression()
            regression = regression.fit(x.to_numpy().reshape(-1, 1), y)
            if abs(regression.coef_[0] - 1) < _MIN_COEFFICIENT:
                # Then we've mined an invariant like x = y + b. Filter this.
                continue
            print(
                f'{variable_name} = {other_variable} * {regression.coef_[0]} + {regression.intercept_}'
            )
            invariants_mined += 1
    return invariants_mined


def mine_linear_kernel_invariants(data: pd.DataFrame,
                                  kernel_variables: Set[str],
                                  start_column: int = 0,
                                  end_column: Optional[int] = None,
                                  pearson_threshold: float = 0.5) -> int:
    """
    Mines invariants in the form y = a * x + b.

    Args:
        data: The variables to extract invariants from.
        kernel_variables: Core measurements.
        start_column: The column to start from. Helps us split tasks.
        end_column: The column to end search on. Helps us split tasks.
        pearson_threshold: The absolute value threshold of the Pearson coefficient to reject invariants as uncorrelated. 
    """
    if end_column is None:
        end_column = len(data.columns)

    invariants_mined = 0
    for idx, variable_name in enumerate(data.columns[start_column:end_column]):
        for other_variable in kernel_variables:
            if other_variable not in data.columns or variable_name in kernel_variables:
                continue

            indices = (data[variable_name] != -
                       42) & (data[other_variable] != -42)
            y = data[variable_name][indices]
            x = data[other_variable][indices]
            if len(x) < _MIN_SAMPLES or len(y) < _MIN_SAMPLES or (x == x.iloc[0]).all() or (y == y.iloc[0]).all():
                continue

            # pearson_correlation, _ = scipy.stats.pearsonr(x, y)
            # if math.isnan(pearson_correlation) or abs(pearson_correlation) < pearson_threshold:
            #     continue

            regression = sklearn.linear_model.LinearRegression()
            regression = regression.fit(x.to_numpy().reshape(-1, 1), y)
            if abs(regression.coef_[0] - 1) < _MIN_COEFFICIENT:
                # Then we've mined an invariant like x = y + b. Filter this.
                continue

            if sklearn.metrics.mean_squared_error(y, regression.predict(x.to_numpy().reshape(-1, 1))) < 1:
                print(
                    f'CONFIDENT {variable_name} = {other_variable} * {regression.coef_[0]} + {regression.intercept_}',
                    flush=True,
                )

            print(
                f'{variable_name} = {other_variable} * {regression.coef_[0]} + {regression.intercept_}',
                flush=True,
            )
            invariants_mined += 1
    return invariants_mined
