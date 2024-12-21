import io
import json
import logging
from pathlib import Path
import sys
from traceback import TracebackException
from typing import Iterable


logger = logging.getLogger(__name__)


def run_sim(in_path: str):
    import opentrons.simulate
    from opentrons.protocols.duration import DurationEstimator

    with Path(in_path).open("r") as in_f:
        input_info = json.load(in_f)
    script_file = io.StringIO()
    script_file.write(input_info['script_content'])
    script_file.seek(0)
    if input_info['quiet']:
        logger.setLevel(logging.ERROR)
    duration_estimator = DurationEstimator()
    aborted = True
    try:
        run_log = opentrons.simulate.simulate(
            script_file,
            custom_labware_paths=input_info['custom_labware_paths'],
            duration_estimator=duration_estimator
            )[0]
        aborted = False
    except Exception as e:
        tb_exc = TracebackException.from_exception(e)
        output_info = {
            'log_lines': None,
            'est_duration_s': None,
            'aborted': True,
            'traceback_lines': list(tb_exc.format())
            }
    if not aborted:
        output_info = {
            'log_lines': [rec['payload']['text'] for rec in run_log],
            'est_duration_s': duration_estimator.get_total_duration(),
            'aborted': False,
            'traceback_lines': None
            }
    with Path(input_info['out_path']).open("w") as out_f:
        json.dump(output_info, out_f)


def get_well_centers(lw_def_name: str,
                     custom_labware_paths: Iterable[str] = ()):
    from opentrons.protocol_api.labware import get_labware_definition
    from opentrons.util.entrypoint_util import labware_from_paths

    extra_defs = labware_from_paths(custom_labware_paths)
    lw_def = get_labware_definition(lw_def_name, extra_defs=extra_defs)
    return {
        well_name: tuple(well_info[k] for k in ('x', 'y', 'z'))
        for (well_name, well_info) in lw_def['wells'].items()
        }


if __name__ == "__main__":
    #logging.basicConfig(level=logging.INFO)
    for noisy_fcker in [
        "opentrons_shared_data.load",
        "opentrons.util.entrypoint_util"
            ]:
        logging.getLogger(noisy_fcker).setLevel(logging.WARNING)
    action = sys.argv[1]
    args = sys.argv[2:]

    if action == "sim":
        in_path = args[0]
        run_sim(in_path)
    elif action == "get_well_centers":
        lw_def_name = args[0]
        custom_labware_paths = args[1:]
        well_centers = get_well_centers(lw_def_name, custom_labware_paths)
        sys.stdout.write(json.dumps(well_centers))
    else:
        raise ValueError(action)