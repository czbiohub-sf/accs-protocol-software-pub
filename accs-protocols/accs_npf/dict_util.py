from typing import Any, Dict, Hashable, Sequence


def dotpath_to_keypath(dotpath: str):
    return tuple(dotpath.split("."))


def get_dict_item_by_dotpath(dict_: Dict, dotpath: str) -> Any:
    return get_dict_item_by_keypath(dict_, dotpath.split("."))


def set_dict_item_by_dotpath(dict_: Dict, dotpath: str, value: Any):
    set_dict_item_by_keypath(dict_, dotpath.split("."), value)


def get_dict_item_by_keypath(dict_: Dict, keypath: Sequence[Hashable]) \
        -> Any:
    path_keys = list(keypath)
    el = dict_
    while path_keys:
        key = path_keys.pop(0)
        el = el[key]
    return el


def set_dict_item_by_keypath(
        dict_: Dict, keypath: Sequence[Hashable], value: Any):
    path_keys = list(keypath)
    el = dict_
    while path_keys:
        key = path_keys.pop(0)
        if not path_keys:
            el[key] = value
            break
        if key not in el:
            el[key] = {}
        el = el[key]


def get_all_dict_keypaths(dict_: Dict):
    return [(key,) + sub_kp for key in dict_.keys()
            for sub_kp in get_all_dict_keypaths(dict_[key])]
