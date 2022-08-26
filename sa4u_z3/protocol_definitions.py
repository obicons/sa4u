
from dataclasses import dataclass
import enum


class ProtocolDefinitionSourceType(enum.Enum):
    ProtocolFile = 0
    FlexModuleAPI = 1


def _is_flex_api_url(location: str) -> bool:
    return location.startswith('http://') or location.startswith('https://')


@dataclass
class ProtocolDefinitionSource:
    kind: ProtocolDefinitionSourceType
    location: str

    @staticmethod
    def from_location(location: str) -> 'ProtocolDefinitionSource':
        '''Creates a new instance of this class by examining the value of `location`.'''
        if _is_flex_api_url(location):
            return ProtocolDefinitionSource(ProtocolDefinitionSourceType.FlexModuleAPI, location)
        else:
            return ProtocolDefinitionSource(ProtocolDefinitionSourceType.ProtocolFile, location)
