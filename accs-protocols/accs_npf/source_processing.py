from collections import defaultdict
import re
from types import GeneratorType
from typing import Any, Dict, Hashable, List, Optional, Sequence, TextIO, \
                   Tuple, Union

import libcst
import libcst.matchers

from .dict_util import get_dict_item_by_keypath, get_all_dict_keypaths


class ImportList:
    # TODO: Reimplement more intelligently with a syntax parser
    # so this works more robustly
    PATTERN_FROMX = r'from\s+(?P<base>[-.\w]+)\s+import\s+(?P<imports>.+)'
    PATTERN_IMPORTX = r'import\s+(?P<imports>.+)'

    def __init__(self):
        self._po_fromx = re.compile(self.PATTERN_FROMX)
        self._po_importx = re.compile(self.PATTERN_IMPORTX)
        self.imports_by_base = defaultdict(set)

    def parse_source_line(self, line_text: str) -> bool:
        match = self._po_fromx.match(line_text)
        if match:
            base = match.group('base')
        else:
            base = None
            match = self._po_importx.match(line_text)
            if not match:
                return False
        import_names = (
            x.strip()
            for x in match.group('imports').strip().strip(",").split(","))
        self.imports_by_base[base].update(import_names)
        return True

    def get_merged_lines(self):
        lines = []
        import_specs = [(k, sorted(v)) for (k, v) in self.imports_by_base.items() if k is not None]
        import_specs.extend((v, None) for v in self.imports_by_base[None])
        import_specs.sort(key=lambda x: x[0])
        for base, subs in import_specs:
            if subs is None:
                lines.append(f"import {base}")
            else:
                subs_str = ", ".join(subs)
                lines.append(f"from {base} import {subs_str}")
        return lines


# sorry this is some real goofy sh*t
class SourceDict:
    TEXT_ENCODING = 'utf-8'

    def __init__(self, name: str, source: Optional[str] = None,
                 orig_path: Optional[str] = None):
        self.name = name
        if source is None:
            source = f"{name} = {{}}\n"
        self.orig_source = source
        self.orig_path = orig_path
        self.tree = libcst.parse_module(source)

    @classmethod
    def from_file(cls, name: str, file_or_path: Union[TextIO, str],
                  _orig_path: Optional[str] = None):
        if isinstance(file_or_path, str):
            with open(file_or_path, "r", encoding=cls.TEXT_ENCODING) as f:
                return cls.from_file(name, f, _orig_path)
        return cls(name=name, source=file_or_path.read(), orig_path=_orig_path)

    def _get_dict_node(self) -> libcst.Dict:
        class FindLastAssignTo(libcst.CSTVisitor):
            def __init__(self, target_name: str):
                super().__init__()
                self.target_name = target_name
                self.last_match = None

            def visit_Assign(self, node: libcst.Assign):
                for target in node.targets:
                    if target.target.value == self.target_name:
                        self.last_match = node
                        break
                return False

        v = FindLastAssignTo(self.name)
        self.tree.visit(v)
        if v.last_match is None:
            raise ValueError(
                f"Assignment to '{self.name}' not found in source")
        assign_node = v.last_match
        if not libcst.matchers.matches(
                    assign_node.value, libcst.matchers.Dict()):
            raise ValueError(
                f"Value assigned to '{self.name}' is not a dictionary")
        return assign_node.value

    def _get_paths_of_changes(self, other: Dict[Hashable, Any]) \
            -> List[Tuple[Hashable]]:
        curr_dict = self.get_dict()
        return [
            keypath for keypath in get_all_dict_keypaths(other)
            if keypath not in get_all_dict_keypaths(curr_dict)
            or get_dict_item_by_keypath(curr_dict, keypath)
            != get_dict_item_by_keypath(other, keypath)]

    def _update_or_delete_item(
        self, keypath: Sequence[Hashable], value: Optional[Any] = None,
        delete: bool = False
            ):
        rem_keypath = list(keypath)
        node = self._get_dict_node()
        if len(keypath) < 1:
            raise ValueError("keypath must be at least one key long")
        el: libcst.DictElement
        while rem_keypath:
            found = False
            if not isinstance(node, libcst.Dict):
                self.tree = self.tree.deep_remove(el)
                return self.update_item(keypath, value)
            els = node.elements
            key = rem_keypath.pop(0)
            for el in els:
                if el.key.evaluated_value == key:
                    prev_node = node
                    node = el.value
                    found = True
                    break
                if found:
                    break
            if not found:
                rem_keypath.insert(0, key)
                break

        if delete:
            if found and not rem_keypath:
                self.tree = self.tree.deep_remove(el)
                return
            else:
                raise ValueError(
                    f"element {self.format_keypath(keypath)} does not exist")

        if rem_keypath:
            orig_node = node
            new_key = libcst.parse_expression(repr(rem_keypath.pop(0)))
            if not rem_keypath:
                new_val = libcst.Integer("42")
            else:
                new_val = libcst.Dict(())
            new_el = libcst.DictElement(new_key, new_val)
            node = node.with_changes(elements=node.elements+(new_el,))
            self.tree = self.tree.deep_replace(orig_node, node)
            return self.set_item(keypath, value)

        def tuplize(x):
            if any(isinstance(x, y) for y in (GeneratorType, set)):
                return tuple(tuplize(y) for y in x)
            else:
                return x
        value = tuplize(value)
        val_node = libcst.parse_expression(repr(value))
        self.tree = self.tree.with_deep_changes(el, value=val_node)

    def get_dict_source(self):
        return self.tree.code_for_node(self._get_dict_node())

    def get_dict(self) -> Dict[Hashable, Any]:
        locals_: Dict[str, Any] = {}
        exec(self.get_module_source(), {}, locals_)
        return locals_[self.name]

    def get_module_source(self) -> str:
        return self.tree.code_for_node(self.tree)

    def write_module_to_file(self, file_or_path: Union[TextIO, str]):
        if isinstance(file_or_path, str):
            with open(file_or_path, "w", encoding=self.TEXT_ENCODING) as f:
                return self.write_module_to_file(f)
        file_or_path.write(self.get_module_source())

    def format_keypath(self, keypath: Sequence[Hashable]) -> str:
        return self.name + "".join(f"[{x!r}]" for x in keypath)

    def delete_item(self, keypath: Sequence[Hashable]):
        self._update_or_delete_item(keypath, delete=True)

    def get_item(self, keypath: Sequence[Hashable]):
        return get_dict_item_by_keypath(self.get_dict(), keypath)

    def set_item(self, keypath: Sequence[Hashable], value: Any):
        self._update_or_delete_item(keypath, value=value)

    def update_dict(self, other: Dict):
        for keypath in self._get_paths_of_changes(other):
            self.update_item(
                keypath, get_dict_item_by_keypath(other, keypath))

    def update_items(self, items: Sequence[Tuple[Sequence[Hashable], Any]]):
        for keypath, value in items:
            self.set_item(keypath, value)
