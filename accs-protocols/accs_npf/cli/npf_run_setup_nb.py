import argparse
import importlib
from pathlib import Path
import subprocess
import sys
from typing import Iterable, Optional, Union

import tornado
from jupyter_server.base.handlers import APIHandler
from jupyter_core.utils import ensure_async

from .. import paths as npf_paths


class _VoilaShutdownKernelHandlerThatAlsoQuitsVoila(APIHandler):
    @tornado.web.authenticated
    async def post(self, kernel_id):
        await ensure_async(self.kernel_manager.shutdown_kernel(kernel_id))
        self.set_status(204)
        self.finish()
        loop = tornado.ioloop.IOLoop.current()
        loop.add_callback(loop.stop)


# ha ha hehehe
def _patch_and_run_voila(nb_script_path: Path, no_browser: bool = False,
                         no_shutdown: bool = False,
                         browser_path: Optional[Union[Path, str]] = None):
    if not no_shutdown:
        import voila.shutdown_kernel_handler
        voila.shutdown_kernel_handler.VoilaShutdownKernelHandler = (
             _VoilaShutdownKernelHandlerThatAlsoQuitsVoila)
    import voila.app
    importlib.reload(voila.app)
    args = [
        "--Voila.tornado_settings",
        "disable_check_xsrf=true",
        str(nb_script_path.resolve())
        ]
    if no_browser:
        args.append("--no-browser")
    if browser_path is not None:
        browser_path = str(Path(browser_path).resolve())
        args.append(f"--Voila.browser={browser_path}")
    voila.app.main(args)


def get_notebook_copy(notebook_name: str):
    nb_dir = npf_paths.get_setup_notebooks_dir()
    out_path = nb_dir.joinpath(f"{notebook_name}.ipynb")
    nb_content = npf_paths.load_resource_text(
        "jupyter_nbs", f"{notebook_name}.ipynb")
    with out_path.open("w") as out_f:
        out_f.write(nb_content)
    return out_path, nb_dir


def run_notebook(notebook_name: str,
                 no_browser: bool = False, no_shutdown: bool = False,
                 browser_path: Optional[Union[Path, str]] = None):
    nb_path, nb_dir_path = get_notebook_copy(notebook_name)
    _patch_and_run_voila(
        nb_path,
        no_browser=no_browser,
        no_shutdown=no_shutdown,
        browser_path=browser_path
        )


def get_arg_parser(argv, proto_names: Iterable[str] = ()):
    parser = argparse.ArgumentParser(
        prog=Path(argv[0]).name,
        description="Launch an interactive form to generate an ACCS protocol "
                    "script",
        )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--list",
        action="store_true",
        help="print a list of valid protocol names and exit"
        )
    parser.add_argument(
        "--browser",
        metavar="PATH",
        default=None,
        help="specify which browser to use"
        )
    parser.add_argument(
        "--no-browser",
        action="store_true",
        help="don't start a browser"
        )
    parser.add_argument(
        "--no-shutdown",
        action="store_true",
        help="don't automatically quit when the browser tab is closed"
        )
    group.add_argument(
        "protocol_name",
        metavar="PROTOCOL_NAME",
        help="name of the protocol to configure, e.g. cci_normalization",
        nargs='?',
        choices=proto_names,
        )
    return parser


def main():
    nb_names = npf_paths.get_resource_names("jupyter_nbs", "setup_*.ipynb")
    proto_names = [
        nb_name.rsplit(".", 1)[0].split("_", 1)[-1] for nb_name in nb_names]
    parser = get_arg_parser(sys.argv, proto_names=proto_names)
    cmdline_args = parser.parse_args(sys.argv[1:])
    if cmdline_args.list:
        for proto_name in proto_names:
            print(proto_name)
        return 0

    run_notebook(
        f"setup_{cmdline_args.protocol_name}",
        no_browser=cmdline_args.no_browser,
        browser_path=cmdline_args.browser,
        no_shutdown=cmdline_args.no_shutdown,
        )
    return 0