from abc import ABC, abstractmethod
import hashlib
import os
from pathlib import Path
from typing import Any, Dict, Hashable, Iterable, Sequence, TextIO, Optional, \
    Union, List, Callable, Tuple, Set

from .dict_util import dotpath_to_keypath
from .source_processing import ImportList, SourceDict
from . import paths as npf_paths
from .protocol_analyzer import ProtocolAnalyzer


class ScriptPart(ABC):
    @abstractmethod
    def get_text(self) -> str:
        ...

    def get_source_text(self) -> str:
        return self.get_text()


class StringScriptPart(ScriptPart):
    def __init__(self, source: str):
        self.source = source

    def get_text(self) -> str:
        return self.source


class FileScriptPart(StringScriptPart):
    def __init__(self, file_or_path: Union[TextIO, str],
                 encoding: Optional[str] = None):
        if isinstance(file_or_path, str):
            self.path = file_or_path
            with open(self.path, "r", encoding=encoding) as f:
                return self.__class__.__init__(self, f)
        self.path = getattr(self, 'path', None)
        super().__init__(file_or_path.read())


class RunConfigScriptPart(ScriptPart):
    DICT_NAME = 'run_config'

    def __init__(self, sourcedict: SourceDict):
        self._sourcedict = sourcedict
        self.source = sourcedict.orig_source
        self.path = sourcedict.orig_path
        self.update_dict = sourcedict.update_dict
        self.update_items = sourcedict.update_items
        self.get_item = sourcedict.get_item
        self.set_item = sourcedict.set_item
        self.get_dict = sourcedict.get_dict

    @classmethod
    def from_string(cls, source: Optional[str] = None) \
            -> 'RunConfigScriptPart':
        return cls(SourceDict(cls.DICT_NAME, source))

    @classmethod
    def from_file(cls, file_or_path: Union[TextIO, str]) \
            -> 'RunConfigScriptPart':
        return cls(SourceDict.from_file(cls.DICT_NAME, file_or_path))

    def get_text(self) -> str:
        return self._sourcedict.get_module_source()

    def get_source_text(self) -> str:
        return self.source


class ProtocolScriptBuilder:
    PROTOCOL_NAME: Optional[str] = "(unnamed)"
    RUN_CONFIG_NAME: Optional[str] = None
    INCLUDE_RES_DIR_NAME: str = "protocol_parts"
    INCLUDES: Sequence[str] = ()
    ABS_INCLUDE_PATHS: Sequence[str] = ()
    RUN_CONFIG_SUFFIX = ".run_config"
    PROTOCOL_SUFFIX = ".protocol"
    SNIPPET_EXTENSION = "py.inc"
    TEXT_ENCODING = 'utf-8'
    version_hash_cls = hashlib.sha1

    def __init__(self, protocol_name: Optional[str] = None,
                 run_config_name: Optional[str] = None,
                 includes: Optional[Iterable[str]] = None,
                 include_res_dir_name: Optional[str] = None,
                 abs_include_paths: Optional[Iterable[str]] = None):
        self.protocol_name = \
            protocol_name if protocol_name is not None else self.PROTOCOL_NAME
        self.run_config_name = \
            run_config_name if run_config_name is not None \
            else (self.RUN_CONFIG_NAME if self.RUN_CONFIG_NAME is not None
                  else self.protocol_name)
        self.includes = list(
            includes if includes is not None else self.INCLUDES)
        self.include_res_dir_name = \
            include_res_dir_name if include_res_dir_name is not None \
            else self.INCLUDE_RES_DIR_NAME
        self.abs_include_paths = list(
            abs_include_paths if abs_include_paths is not None
            else self.ABS_INCLUDE_PATHS)
        self._parts = None
        self._runconfig_part = None

    def update_runconfig_params(self, other: Dict):
        self._get_runconfig_part().update_dict(other)

    def get_runconfig_params(self) -> Dict:
        return self._get_runconfig_part().get_dict()

    def set_runconfig_param(self, keypath: Sequence[Hashable], value: Any):
        self._get_runconfig_part().set_item(keypath, value)

    def set_runconfig_param_by_dotpath(self, dotpath: str, value: Any):
        self._get_runconfig_part().set_item(dotpath_to_keypath(dotpath), value)

    def get_runconfig_param_by_dotpath(self, dotpath: str):
        return self._get_runconfig_part().get_item(dotpath_to_keypath(dotpath))

    def _get_runconfig_part(self):
        if self._runconfig_part is None:
            with npf_paths.open_resource(
                    self.INCLUDE_RES_DIR_NAME,
                    f"{self.run_config_name}{self.RUN_CONFIG_SUFFIX}"
                    f".{self.SNIPPET_EXTENSION}"
                    ) as src_f:
                self._runconfig_part = RunConfigScriptPart.from_file(src_f)
            self.init_runconfig()
        return self._runconfig_part

    def init_runconfig(self):
        pass

    def get_script_parts(self):
        if self._parts is None:
            runconfig_part = self._get_runconfig_part()
            self._parts = []
            for include_name in self.includes:
                with npf_paths.open_resource(
                        self.INCLUDE_RES_DIR_NAME,
                        f"{include_name}.{self.SNIPPET_EXTENSION}"
                        ) as part_f:
                    self._parts.append(
                        FileScriptPart(part_f, encoding=self.TEXT_ENCODING)
                        )
            self._parts += [
                FileScriptPart(path, encoding=self.TEXT_ENCODING)
                for path in self.abs_include_paths
                ]
            self._parts.append(runconfig_part)
            with npf_paths.open_resource(
                    self.INCLUDE_RES_DIR_NAME,
                    f"{self.protocol_name}{self.PROTOCOL_SUFFIX}"
                    f".{self.SNIPPET_EXTENSION}"
                    ) as proto_f:
                self._parts.append(
                    FileScriptPart(proto_f, encoding=self.TEXT_ENCODING)
                    )
        return list(self._parts)

    @staticmethod
    def collapse_excess_empty_elements(elements: Iterable, max_run_len: int):
        run_len = 0
        output_list = []
        for el in elements:
            if len(el):
                run_len = 0
            else:
                run_len += 1
            if run_len <= max_run_len:
                output_list.append(el)
        return output_list

    @classmethod
    def merge_snippet_strings(cls, parts: Iterable[str]):
        parts = list(parts)
        separator_lines = ["", "", "##########", "", ""]
        import_list = ImportList()
        output_lines = []
        for (i, part) in enumerate(parts):
            for line in (x.rstrip() for x in part.splitlines()):
                if not import_list.parse_source_line(line):
                    output_lines.append(line)
            if i < len(parts) - 1:
                output_lines.extend(separator_lines)
        import_lines = import_list.get_merged_lines()
        if import_lines:
            output_lines = import_lines + separator_lines + output_lines
        output_lines = cls.collapse_excess_empty_elements(
            output_lines, max_run_len=2)
        return "\n".join(output_lines) + "\n"

    @classmethod
    def merge_script_parts(cls, parts: Sequence[ScriptPart]):
        return cls.merge_snippet_strings(part.get_text() for part in parts)

    def get_source_version_hash(self) -> str:
        hash_obj = self.version_hash_cls()
        for part in self._parts:
            hash_obj.update(part.get_source_text().encode('utf-8'))
        return hash_obj.hexdigest()

    def generate_script(self):
        merged = self.merge_script_parts(self.get_script_parts())
        hash_obj = self.version_hash_cls()
        hash_obj.update(merged.encode('utf-8'))
        config_hash = hash_obj.hexdigest()
        return \
            f"TEXT_ENCODING={self.TEXT_ENCODING!r}\n" \
            f"SOURCE_HASH={self.get_source_version_hash()!r}\n" \
            f"CONFIG_HASH={config_hash!r}\n\n{merged}"
        return merged

    def write_script(self, file_or_path: Union[Path, str, TextIO]):
        if isinstance(file_or_path, str) or isinstance(file_or_path, Path):
            path = Path(file_or_path)
            with path.open("w", encoding=self.TEXT_ENCODING) as outf:
                return self.write_script(file_or_path=outf)
        file_or_path.write(self.generate_script())

    def reset_run_config(self):
        self._runconfig_part = None
        self._parts = None


class TipcullingProtocolScriptBuilder(ProtocolScriptBuilder):
    PROTOCOL_NAME = "tip_culling"
    INCLUDES = ("protocol_common",)

    def __init__(self, input_script_file: TextIO,
                 split_factors: Dict[str, float],
                 run_id: Optional[str] = None,
                 set_run_info: Optional[Dict[str, Optional[str]]] = None,
                 *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._append_tip_culling_params(input_script_file, split_factors)
        if set_run_info is not None:
            for key, value in set_run_info.items():
                self.set_runconfig_param_by_dotpath(
                    f'logging.run_info.{key}', value)
        if run_id is not None:
            self.set_runconfig_param_by_dotpath('logging.run_id', run_id)

    def _append_tip_culling_params(self, input_script_file: TextIO,
                                   split_factors: Dict[str, float]):
        # TODO: these plate slots are specific to splitting - could change!
        pa = ProtocolAnalyzer.from_protocol_file(
                input_script_file,
                plate_slots=("1", "2", "3"),
                quiet=True)

        # TODO: same for slot "3" here, being the source well
        wells_to_cull = pa.get_tip_wells_for_plate_wells('3', [
                f"{row}{col}"
                for row in "ABCDEFGH"
                for col in range(1,13)
                if split_factors[f"{row}{col}"] == 0
            ]
        )

        self.check_for_tip_reuses(pa, ['3'])
        self.set_runconfig_param_by_dotpath(
            'tip_culling.wells_to_cull', wells_to_cull)

    def check_for_tip_reuses(self, pa: ProtocolAnalyzer, slots: Iterable[Union[str,int]] = []):
        """ checks for reusing tips on plates
        """
        # checking if the tip touches the source plate multiple times
        multiple_tip_touches: List[
                Tuple[
                    Tuple[str,str], Set[Tuple[str,str]]
                    ]
                ] = []

        formatted_slots = [str(maybe_int) for maybe_int in slots]
        for tip_source, touched_wells in pa.tip_touches.items():
            if sum(slot in formatted_slots for (slot, well_name) in touched_wells) > 1:
                multiple_tip_touches.append((tip_source, touched_wells))

        if len(multiple_tip_touches) > 0:
            formatted_touch_list = [
                    f'tip {tip} touched {touches}'
                    for tip, touches in multiple_tip_touches
            ]
            raise RuntimeError(
                    f"multiple tip-touches of slots: "
                    f"{formatted_touch_list}"
                )


class _CciXScriptBuilder(ProtocolScriptBuilder):
    INCLUDES = (
        "protocol_common",
        "cci_client",
        "cell_splitting_common",
        )

    def init_runconfig(self):
        super().init_runconfig()
        server_addrs = npf_paths.load_local_config("cci_addresses")
        self.set_runconfig_param_by_dotpath(
            "cci.server_urls",
            {
                robot_id: f"http://{cci_addr}/cci"
                for (robot_id, cci_addr) in server_addrs.items()
                }
            )


class CciNormalizationScriptBuilder(_CciXScriptBuilder):
    PROTOCOL_NAME = "cci_normalization"


class CciPresuspNormScriptBuilder(_CciXScriptBuilder):
    PROTOCOL_NAME = "cci_presusp_norm"
    RUN_CONFIG_NAME = "cci_normalization"


class CciCountOnlyScriptBuilder(_CciXScriptBuilder):
    PROTOCOL_NAME = "cci_count_only"
    RUN_CONFIG_NAME = "cci_normalization"


class CciInfImgSplitScriptBuilder(_CciXScriptBuilder):
    PROTOCOL_NAME = "cci_iis"
    RUN_CONFIG_NAME = "cci_normalization"


class CciNormalization4x96ScriptBuilder(_CciXScriptBuilder):
    PROTOCOL_NAME = "cci_normalization_4x96"


class FixedSplittingScriptBuilder(ProtocolScriptBuilder):
    PROTOCOL_NAME = "fixed_splitting"
    INCLUDES = (
        "protocol_common",
        "cell_splitting_common",
        )


class ColwiseFixedSplittingScriptBuilder(ProtocolScriptBuilder):
    PROTOCOL_NAME = "fixed_splitting_colwise"
    RUN_CONFIG_NAME = "fixed_splitting"
    INCLUDES = (
        "protocol_common",
        "cell_splitting_common",
        )


class FixedDiscardSplittingScriptBuilder(ProtocolScriptBuilder):
    PROTOCOL_NAME = "fixed_splitting_discard"
    INCLUDES = (
        "protocol_common",
        "cell_splitting_common",
        )


class Expansion96To8x12ScriptBuilder(ProtocolScriptBuilder):
    PROTOCOL_NAME = "expansion_96to8x12"
    INCLUDES = (
        "protocol_common",
        "cell_splitting_common",
        )


class ExpansionMs96To3x96ScriptBuilder(ProtocolScriptBuilder):
    PROTOCOL_NAME = "expansion_ms_96to3x96"
    INCLUDES = (
        "protocol_common",
        "cell_splitting_common",
        )
