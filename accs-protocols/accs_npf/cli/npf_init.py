import argparse
import logging
from pathlib import Path
import shutil
import sys

from .. import paths as accs_npf_paths
from .. import protocol_analyzer


logger = logging.getLogger(__name__)


def get_arg_parser(argv):
    parser = argparse.ArgumentParser(
        prog=Path(argv[0]).name,
        description="Purge and repopulate the ACCS NPF local data directories",
        )
    parser.add_argument(
        "--rm-user",
        action="store_true",
        help="also clear protocol_scripts, local_config and labware_defs dirs "
             "(normally preserved)"
        )
    parser.add_argument(
        "--init-sim",
        action="store_true",
        help="immediately rebuild the sim environment after deleting"
        )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="print debug messages"
        )
    return parser


def main():
    parser = get_arg_parser(sys.argv)
    cmdline_args = parser.parse_args(sys.argv[1:])

    logging.basicConfig(
        level=logging.DEBUG if cmdline_args.debug else logging.INFO)

    delete_list = [
        (accs_npf_paths.get_setup_notebooks_dir(), "*.ipynb"),
        (accs_npf_paths.get_cache_dir(), "*.json"),
        (accs_npf_paths.get_sim_env_dir(), None),
        ]
    if cmdline_args.rm_user:
        delete_list.extend([
            (accs_npf_paths.get_protocol_scripts_dir(), "*.py"),
            (accs_npf_paths.get_labware_defs_dir(), "*.json"),
            (accs_npf_paths.get_local_config_dir(), "*.json"),
            ])

    for dir_path, name_glob in delete_list:
        desc = str(
            dir_path.joinpath(name_glob) if name_glob is not None
            else dir_path
            )
        logger.info(f"Deleting: {desc}")
        if name_glob is None:
            shutil.rmtree(dir_path)
        else:
            for path in dir_path.glob(name_glob):
                path.unlink()
    logger.info("Copying labware definitions")
    accs_npf_paths.copy_custom_labware_to_home(overwrite=True)
    logger.info("Copying local config templates")
    accs_npf_paths.copy_config_templates_to_home(overwrite=False)
    logger.info(
        "Rebuilding sim environment" if cmdline_args.init_sim
        else "Restoring sim environment skeleton"
        )
    protocol_analyzer.SimProxy(init_now=cmdline_args.init_sim)
