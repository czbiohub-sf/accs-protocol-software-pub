from contextlib import contextmanager
import fnmatch
import importlib.resources
import json
import logging
from pathlib import Path


_resources_dir = importlib.resources.files(__package__).joinpath("resources")
logger = logging.getLogger(__name__)


def get_accs_home_dir():
    path = Path.home().joinpath("accs_data")
    path.mkdir(parents=True, exist_ok=True)
    return path


def _get_home_subdir(subdir_name: str):
    path = get_accs_home_dir().joinpath(subdir_name)
    if not path.is_dir():
        logger.debug(f"Creating {str(path)!r}")
        path.mkdir(parents=True, exist_ok=True)
    return path


def get_setup_notebooks_dir():
    return _get_home_subdir("setup_nb")


def get_protocol_scripts_dir():
    return _get_home_subdir("protocol_scripts")


def get_labware_defs_dir():
    return _get_home_subdir("labware_defs")


def get_cache_dir():
    return _get_home_subdir("cache")


def get_sim_env_dir():
    return _get_home_subdir("sim_env")


def get_local_config_dir():
    return _get_home_subdir("local_config")


def _get_resource(subdir_name: str, resource_name: str):
    return _resources_dir.joinpath(subdir_name, resource_name)


@contextmanager
def open_resource(subdir_name: str, resource_name: str):
    with importlib.resources.as_file(
            _get_resource(subdir_name, resource_name)) as path:
        with path.open("r") as f:
            yield f


def load_resource_text(subdir_name: str, resource_name: str):
    return _get_resource(subdir_name, resource_name).read_text()


def load_local_config(config_name: str):
    path = get_local_config_dir().joinpath(f"{config_name}.json")
    if not path.is_file():
        copy_local_config_templates_to_home()
    with path.open("r") as f:
        return json.load(f)


def get_resource_names(subdir_name: str, glob: str = "*"):
    return [
        p.name for p in _resources_dir.joinpath(subdir_name).iterdir()
        if fnmatch.fnmatch(p.name, glob) and p.is_file()
        ]


def _copy_all_x_to_home(resource_subdir_name: str, home_subdir_name: str,
                        overwrite: bool = False):
    _home_subdir = _get_home_subdir(home_subdir_name)
    for name in get_resource_names(resource_subdir_name):
        dest_path = _home_subdir.joinpath(name)
        if overwrite or not dest_path.exists():
            action = "Overwriting" if dest_path.exists() else "Adding"
            logger.debug(f"{action} {str(dest_path)!r}")
            file_content = load_resource_text(resource_subdir_name, name)
            with dest_path.open("w") as dest_f:
                dest_f.write(file_content)
        else:
            logger.debug(f"Leaving existing {str(dest_path)!r}")


def copy_custom_labware_to_home(overwrite: bool = False):
    _copy_all_x_to_home("labware_defs", "labware_defs", overwrite=overwrite)


def copy_config_templates_to_home(overwrite: bool = False):
    _copy_all_x_to_home(
        "config_templates", "local_config", overwrite=overwrite)
