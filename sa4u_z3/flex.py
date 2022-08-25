from dataclasses import dataclass
import dataclasses
import enum
import json
from typing import List, Optional
import aiohttp


class Package(enum.Enum):
    MAVLINK_23 = 0
    OPENUXAS_LMCP_V3 = 1


@dataclass
class Message:
    name: str = ''
    url: str = ''


@dataclass
class Struct:
    @dataclass
    class Field:
        name: str = ''
        unit_name: Optional[str] = None
    name: str = ''
    fields: List[Field] = dataclasses.field(default_factory=list)


_PACKAGE_TO_STR = {
    Package.MAVLINK_23: 'MAVLink::v23',
    Package.OPENUXAS_LMCP_V3: 'OpenUxAS::LMCP::v3',
}

_UNITS_ANNOTATION_NAME = 'tangram::flex::helpers::v1.annotations.Units'


def _get_unit_name_in_annotation(annotation: List[dict]) -> Optional[str]:
    for a in annotation:
        if a['name'] == _UNITS_ANNOTATION_NAME and len(a['values']) > 0:
            return a['values'][0]
    return None


class FlexAPI:
    def __init__(self, api_url: str, session: aiohttp.ClientSession) -> None:
        self._api_url = api_url
        self._session = session
        self._api_version = 'v1'

    def _make_message_url(self, package: Package) -> str:
        return f'{self._api_url}/{self._api_version}/package/{_PACKAGE_TO_STR[package]}/messages'

    def _make_struct_url(self, package: Package, struct_name: str) -> str:
        return f'{self._api_url}/{self._api_version}/package/{_PACKAGE_TO_STR[package]}/struct/{struct_name}'

    async def download_messages(self, package: Package) -> List[Message]:
        results = []
        async with self._session.get(self._make_message_url(package)) as response:
            json_str = await response.text()
            response_data: List[str] = json.loads(json_str)
            for message_name in response_data:
                results.append(
                    Message(
                        message_name,
                        self._make_struct_url(package, message_name),
                    ),
                )
        return results

    async def download_struct_by_url(self, url: str) -> Struct:
        async with self._session.get(url) as response:
            json_str = await response.text()
            response_data: dict = json.loads(json_str)
            return Struct(
                name=response_data['name'],
                fields=[
                    Struct.Field(
                        field['name'],
                        _get_unit_name_in_annotation(field['annotations']),
                    )
                    for field in response_data['fields']
                ],
            )
