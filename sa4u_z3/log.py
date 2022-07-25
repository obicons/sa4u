import enum
import sys


class LogLevel(enum.Enum):
    INFO = 1
    WARNING = 2
    ERROR = 3


def log(level: LogLevel, *args):
    level_to_str = {
        LogLevel.INFO: 'INFO:',
        LogLevel.WARNING: 'WARNING:',
        LogLevel.ERROR: 'ERROR:',
    }
    print(level_to_str[level], sep='', end=' ', file=sys.stderr)
    print(*args, flush=True, file=sys.stderr)
