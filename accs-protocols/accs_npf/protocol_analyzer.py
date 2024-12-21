# TODO: Get the cell-splitting-specific stuff out of here
# TODO: Finish stripping tipmapping / sanity checker stuff and moving them to separate script(s)

from abc import ABC, abstractmethod
import argparse
import ast
import collections
from contextlib import contextmanager
import io
import json
import logging
import os
from pathlib import Path
import re
import string
import subprocess
import sys
from tempfile import TemporaryDirectory
from typing import Any, Dict, Iterable, Optional, Sequence, TextIO, Tuple, Union
from types import SimpleNamespace
import venv

from . import paths as accs_npf_paths


EXTRA_PKGS = ["requests"]


logger = logging.getLogger(__name__)


def sorted_locs(loc_tuples):
    return sorted(
        loc_tuples,
        key=lambda x: (int(x[0].split("*", 1)[0]), parse_well_name(x[1])))


def format_loc(loc_tuple):
    slot, well_name = loc_tuple
    return f"{slot}:{well_name}"


def parse_well_name(well_name):
    match = re.match(r"([A-Za-z]*)([0-9]*)", well_name)
    if not match:
        raise ValueError(f"Can't parse well name: {well_name!r}")
    alpha_part = match.group(1)
    num_part = match.group(2)
    col_num = int(num_part) if num_part else None
    return alpha_part, col_num


def get_col_start_well_name(well_name):
    alpha_part, col_num = parse_well_name(well_name)
    return f"A{col_num}"


def get_tip_well_for_plate_well(tip_col, plate_well):
    tip_slot, tip_col_name = tip_col
    tip_row, tip_col_num = parse_well_name(tip_col_name)
    plate_slot, plate_well_name = plate_well
    plate_row, plate_col_num = parse_well_name(plate_well_name)
    return (tip_slot, f"{plate_row}{tip_col_num}")


class LiquidMixture:
    ABS_TOL = 1e-9

    def __init__(self):
        self.constituent_vols = collections.defaultdict(float)

    def add_mixture(self, mixture: 'LiquidMixture'):
        for liquid_name, vol in mixture.constituent_vols.items():
            self.add(liquid_name, vol)

    def add(self, liquid_name: str, vol: float):
        vol = max(0., vol)
        self.constituent_vols[liquid_name] += vol

    def remove(self, vol: float):
        removed_mix = LiquidMixture()
        vol = max(0., vol)
        if self.get_total_vol() == 0.:
            return removed_mix
        fraction = vol / self.get_total_vol()
        for liquid_name, prev_vol in self.constituent_vols.items():
            removed_vol = min(prev_vol, prev_vol * fraction)
            self.constituent_vols[liquid_name] -= removed_vol
            removed_mix.add(liquid_name, removed_vol)
        return removed_mix

    def get_total_vol(self):
        return sum(self.constituent_vols.values())

    def get_constituents(self, include_zero: bool = False):
        return [
            (k, v) for (k, v) in self.constituent_vols.items()
            if v > 0. or include_zero]

    def __eq__(self, obj):
        return isinstance(obj, LiquidMixture) \
            and all(
                abs(self.constituent_vols[k]
                    - obj.constituent_vols[k]) < self.ABS_TOL
                for k in (obj.constituent_vols.keys()
                          | self.constituent_vols.keys())
            )

    def __ne__(self, obj):
        return not self == obj


class TipToWellMap(ABC):
    @abstractmethod
    def get_wells(self, start_well: str) -> Tuple[str, ...]:
        pass


class TipToWellMapFromLabwareDef(TipToWellMap):
    TIP_SPACING = 9.0

    def __init__(self, def_name: str, n_channels: int,
                 tip_spacing: Optional[float] = None,
                 custom_labware_paths: Iterable[str] = ()):
        if tip_spacing is None:
            tip_spacing = self.TIP_SPACING
        # TODO: Handle "center multi on wells" quirk
        cache_path = accs_npf_paths.get_cache_dir().joinpath(
            f"{def_name}.well_centers.json")
        if cache_path.is_file():
            with cache_path.open("r") as cache_f:
                self._well_centers = json.load(cache_f)
        else:
            self._well_centers = SimProxy().get_well_centers(
                def_name, custom_labware_paths=custom_labware_paths)
            with cache_path.open("w") as cache_f:
                json.dump(self._well_centers, cache_f)
        self._well_seqs = {
            well_name: self._get_well_seq(well_name, n_channels, tip_spacing)
            for (well_name, well_center) in self._well_centers.items()
            }

    @staticmethod
    def _dist(pt1: Tuple[float, float, float],
              pt2: Tuple[float, float, float]) -> float:
        return sum((pt2[i] - pt1[i])**2. for i in range(len(pt1)))**0.5

    def _get_well_seq(self, well_name: str, n_channels: int,
                      tip_spacing: float) -> Tuple[str, ...]:
        x, y, z = self._well_centers[well_name]
        return tuple(
            self._get_nearest_well(x, y - i * tip_spacing, z)
            for i in range(n_channels))

    def _get_nearest_well(self, x: float, y: float, z: float) -> str:
        ref_pt = (x, y, z)
        nearest_name = None
        nearest_dist = None
        for (other_well, other_pt) in self._well_centers.items():
            dist = self._dist(other_pt, ref_pt)
            if nearest_name is None or dist < nearest_dist:
                nearest_name = other_well
                nearest_dist = dist
        if nearest_name is None:
            raise RuntimeError("labware has no well positions?")
        return nearest_name

    def get_wells(self, start_well: str) \
            -> Tuple[str, ...]:
        return self._well_seqs[start_well]


class DummyTipToWellMap(TipToWellMap):
    def __init__(self, offset_seq: Sequence[int] = range(8),
                 row_names: Sequence[str] = string.ascii_uppercase):
        if max(offset_seq) >= len(row_names):
            raise ValueError("row_names too short for value(s) in offset_seq")
        self.offset_seq = offset_seq
        self.row_names = tuple(row_names)

    def get_wells(self, start_well: str) \
            -> Tuple[str, ...]:
        n_channels = len(self.offset_seq)
        start_row = start_well.rstrip(string.digits)
        if not start_row:
            raise ValueError(
                f"start_well {start_well} does not begin with a non-digit")
        try:
            start_col = int(start_well[len(start_row):])
        except ValueError:
            raise ValueError(
                f"start_well {start_well} does not end with digits")
        try:
            start_idx = self.row_names.index(start_row)
        except ValueError:
            raise ValueError(f"bad row name {start_row!r} "
                             f"from well name {start_well!r}")
        row_idxs = (start_idx + self.offset_seq[i] for i in range(n_channels))
        try:
            return tuple(self.row_names[x] + str(start_col) for x in row_idxs)
        except IndexError:
            raise ValueError(f"not enough rows for {n_channels} "
                             f"channels starting from well '{start_well}'")


class LiquidTracker:
    def __init__(self, custom_labware_paths: Sequence[str] = ()):
        self.well_contents: Dict[Tuple[int, str], LiquidMixture] = \
            collections.defaultdict(LiquidMixture)
        self.tip_contents: Dict[str, Optional[Sequence[LiquidMixture]]] = \
            collections.defaultdict(lambda: None)
        self.labware_def_names: Dict[int, Optional[str]] = \
            collections.defaultdict(lambda: None)
        self.labware_names: Dict[int, Optional[str]] = \
            collections.defaultdict(lambda: None)
        self.tip_to_well_maps: Dict[str, Dict[int, TipToWellMap]] = \
            collections.defaultdict(dict)
        self.custom_labware_paths = custom_labware_paths

    def add_liquid(self, slot_no: int, well_name: str,
                   liquid_name: str, vol: float):
        self.well_contents[(slot_no, well_name)].add(liquid_name, vol)

    def add_mixture(self, slot_no: int, well_name: str, mix: LiquidMixture):
        self.well_contents[(slot_no, well_name)].add_mixture(mix)

    def remove_mixture(self, slot_no: int, well_name: str, vol: float):
        return self.well_contents[(slot_no, well_name)].remove(vol)

    def get_total_vol(self, slot_no: int, well_name: str):
        return self.well_contents[(slot_no, well_name)].get_total_vol()

    def clear_slot(self, slot_no: int):
        for (slot_no_, well_name) in self.well_contents:
            if slot_no == slot_no_:
                del self.well_contents[(slot_no_, well_name)]
        for ttw_map in self.tip_to_well_maps.values():
            if slot_no in ttw_map:
                del ttw_map[slot_no]

    def set_labware(self, slot_no: int, def_name: str,
                    name: Optional[str] = None):
        self.clear_slot(slot_no)
        self.labware_def_names[slot_no] = def_name
        self.labware_names[slot_no] = name

    def clear_tips(self, pipette_name: str):
        self.tip_contents[pipette_name] = None

    def get_slot_nos(self):
        return sorted(
            set(slot_no for slot_no, _ in self.well_contents.keys()))

    def get_well_names(self, slot_no: int, sorted_: bool = True):
        well_names = [
            well_name
            for (slot_no_, well_name) in self.well_contents.keys()
            if slot_no_ == slot_no]
        if sorted_:
            well_names.sort(key=lambda x: (int(x[1:]), x[0]))
        return well_names

    def get_slot_contents(self, slot_no: int):
        return collections.OrderedDict(
            (well_name, self.well_contents[(slot_no, well_name)])
            for well_name in self.get_well_names(slot_no))

    def get_col_well_names(self, pipette_name: str, slot_no: int,
                           start_well: str, n_channels: int):
        if slot_no not in self.tip_to_well_maps[pipette_name]:
            if slot_no in self.labware_def_names:
                def_name = self.labware_def_names[slot_no]
                assert def_name is not None
                self.tip_to_well_maps[pipette_name][slot_no] = \
                    TipToWellMapFromLabwareDef(
                        def_name,
                        n_channels,
                        custom_labware_paths=self.custom_labware_paths)
            else:
                self.tip_to_well_maps[pipette_name][slot_no] = (
                    DummyTipToWellMap(offset_seq=range(n_channels)))
        return self.tip_to_well_maps[pipette_name][slot_no] \
            .get_wells(start_well)

    def _assert_correct_n_channels(self, pipette_name: str, n_channels: int):
        exp_n_channels = len(self.tip_contents[pipette_name])
        if n_channels != exp_n_channels:
            raise ValueError(f"n_channels={n_channels} "
                             f"(expected {exp_n_channels}) "
                             f"for pipette_name={pipette_name}")

    def process_aspirate_event(self, pipette_name: str, vol: float,
                               slot_no: int, start_well_name: str,
                               n_channels: int):
        if self.tip_contents[pipette_name] is None:
            self.tip_contents[pipette_name] = [
                LiquidMixture() for i in range(n_channels)]
        else:
            self._assert_correct_n_channels(pipette_name, n_channels)
        well_names = self.get_col_well_names(
            pipette_name, slot_no, start_well_name, n_channels)
        for i, well_name in enumerate(well_names[:n_channels]):
            removed_mix = self.remove_mixture(slot_no, well_name, vol)
            self.tip_contents[pipette_name][i].add_mixture(removed_mix)

    def process_dispense_event(self, pipette_name: str, vol: str, slot_no: int,
                               start_well_name: str, n_channels: int):
        if self.tip_contents[pipette_name] is None:
            return
        self._assert_correct_n_channels(pipette_name, n_channels)
        well_names = self.get_col_well_names(
            pipette_name, slot_no, start_well_name, n_channels)
        for i, well_name in enumerate(well_names[:n_channels]):
            removed_mix = self.tip_contents[pipette_name][i].remove(vol)
            self.add_mixture(slot_no, well_name, removed_mix)

    def process_droptips_event(self, pipette_name: str):
        self.clear_tips(pipette_name)


class PipettingTracker(LiquidTracker):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.current_tip_src: Dict[str, Tuple[str, str, int]] = collections.defaultdict(lambda: None)
        # e.g. current_tip_src[pipette_name] = (tipbox_name, well_name, n_tips)
        self.tip_touches: Set[Tuple[str, str, int, str]] = set()
        # e.g. tip_touches = {(tipbox_name, tipbox_well_name, touch_slot_no, touch_well_name), ...}
        self.tipboxes: Dict[int, str] = {}
        # e.g. tipboxes = {1: "1", 2: "2*", ...}
        self.tips_used: Dict[str, Set[str]] = collections.defaultdict(set)
        # e.g. tips_used = {'1': {"A1", "B1"}, '2*': {}...}

    def process_inittiprack_event(self, slot_no: int):
        if slot_no in self.tipboxes:
            self.tipboxes[slot_no] += "*"
        else:
            self.tipboxes[slot_no] = str(slot_no)
        self.tips_used[self.tipboxes[slot_no]] = set()

    def process_pickuptips_event(self, pipette_name: str, slot_no: int,
                                 start_well_name: str, n_channels: int):
        if slot_no not in self.tipboxes:
            raise ValueError(f"Tipbox on slot {slot_no} not initialized")
        slot_name = self.tipboxes[slot_no]
        if self.current_tip_src[pipette_name] is not None:
            raise ValueError(f"Pipette {pipette_name!r} already holding tip(s)")
        self.tips_used[slot_name] |= set(
            self.get_col_well_names(
                pipette_name, slot_no, start_well_name, n_channels))
        self.current_tip_src[pipette_name] = (
            slot_name, start_well_name, n_channels)

    def process_droptips_event(self, pipette_name: str):
        super().process_droptips_event(pipette_name)
        self.current_tip_src[pipette_name] = None

    def process_aspirate_event(self, pipette_name: str, vol: float,
                               slot_no: int, start_well_name: str,
                               n_channels: int):
        super().process_aspirate_event(
            pipette_name, vol, slot_no, start_well_name, n_channels)
        self._add_tip_touch(pipette_name, slot_no, start_well_name)

    def process_dispense_event(self, pipette_name: str, vol: float,
                               slot_no: int, start_well_name: str,
                               n_channels: int):
        super().process_dispense_event(
            pipette_name, vol, slot_no, start_well_name, n_channels)
        self._add_tip_touch(pipette_name, slot_no, start_well_name)

    def _add_tip_touch(self, pipette_name: str, touch_slot_no: int,
                       touch_start_well: str):
        if self.current_tip_src[pipette_name] is None:
            return
        tip_src_slot, tip_src_well, n_channels = \
            self.current_tip_src[pipette_name]
        tipbox_wells = self.get_col_well_names(
            pipette_name, tip_src_slot, tip_src_well, n_channels)
        touch_wells = self.get_col_well_names(
            pipette_name, touch_slot_no, touch_start_well, n_channels)
        assert len(tipbox_wells) >= len(touch_wells)
        for i in range(len(touch_wells)):
            touch_well = touch_wells[i]
            tip_src_well = tipbox_wells[i]
            self.tip_touches.add(
                (tip_src_slot, tip_src_well, touch_slot_no, touch_well))

    def get_tip_locs_touching_wells(self, slot_no: int,
                                    well_names: Iterable[str]):
        return {
            (tip_src_slot, tip_src_well)
            for (tip_src_slot, tip_src_well, touch_slot_no, touch_well)
            in self.tip_touches
            if touch_slot_no == slot_no and touch_well in well_names
            }


class _OtSimEnvBuilder(venv.EnvBuilder):
    def __init__(self, clear: bool = True, extra_pkgs: Iterable[str] = ()):
        super().__init__(clear=clear, with_pip=True)
        self.extra_pkgs = list(extra_pkgs)

    def post_setup(self, context: SimpleNamespace):
        logger.info("Installing opentrons package in sim environment")
        pip_path = str(Path(context.bin_path).joinpath("pip").resolve())
        subprocess.check_call(
            [
                pip_path,
                "--quiet",
                "--quiet",
                "--disable-pip-version-check",
                "install",
                "aionotify==0.2.0",
                "opentrons==4.7.0",
                ] + self.extra_pkgs,
            stdout=subprocess.DEVNULL,
            )
        logger.info("Patching opentrons package so it will run on Python>=3.11")
        site_pkgs_path = subprocess.check_output(
            [
                context.env_exe,
                "-c",
                "import site; print([x for x in site.getsitepackages() "
                "if 'site-packages' in x][-1])"
                ],
            text=True
            ).strip()
        logger.debug(f"Sim env site packages path: {site_pkgs_path!r}")
        patched_something = False
        found_something = False
        for path in (
                list(Path(site_pkgs_path).glob("opentrons/**/*.py"))
                + list(Path(site_pkgs_path).glob("aionotify/**/*.py"))
                ):
            found_something = True
            content = path.read_text()
            sub_pairs = [
                ("Condition(loop=loop)", "Condition()"),
                ("Event(loop=loop)", "Event()"),
                ("Lock(loop=self._loop)", "Lock()"),
                ]
            if path.parts[-2:] in [
                    ("aionotify", "base.py"),
                    ("aionotify", "aioutils.py")
                    ]:
                sub_pairs.append(("yield from", "await"))
            new_content = content
            for old, new in sub_pairs:
                new_content = new_content.replace(old, new)
            new_content = re.sub(
                r'@asyncio\.coroutine\s+def', "async def", new_content)
            if new_content != content:
                with path.open("w") as f:
                    f.write(new_content)
                logger.debug(f"Patched {str(path)}")
                patched_something = True
        if not found_something:
            logger.warning("Didn't match any library paths to patch?!")
        elif not patched_something:
            logger.warning("No files patched")


class SimProxy:
    def __init__(self, init_now: bool = False):
        self.env_dir = accs_npf_paths.get_sim_env_dir()
        self._bin_dir: Optional[Path] = None
        if init_now:
            self._build_sim_env_if_needed()

    def _get_bin_path(self, name: str):
        if self._bin_dir is None:
            context = _OtSimEnvBuilder(clear=False
                                       ).ensure_directories(self.env_dir)
            self._bin_dir = Path(context.bin_path).resolve()
        return self._bin_dir.joinpath(name)

    @contextmanager
    def _work_with_proxy_script(self):
        self._build_sim_env_if_needed()
        accs_npf_paths.copy_custom_labware_to_home()
        with TemporaryDirectory(prefix="npf_sim_") as temp_dir_path:
            with Path(temp_dir_path).joinpath(f"run.py").open("w") as script_f:
                script_f.write(
                    accs_npf_paths.load_resource_text(
                        "sim_proxy_script", "run.py")
                    )
            yield temp_dir_path

    def get_well_centers(
            self, lw_def_name: str,
            custom_labware_paths: Iterable[Union[Path, str]] = ()
            ):
        with self._work_with_proxy_script() as temp_dir_path:
            logger.info(f"Extracting well locations for {lw_def_name!r}")
            output = self._run_python(
                "run.py",
                "get_well_centers",
                lw_def_name,
                *custom_labware_paths,
                cwd=temp_dir_path,
                )
            well_centers = json.loads(output)
        return well_centers

    def simulate(self, script_file: TextIO, quiet: bool = False,
                 custom_labware_paths: Iterable[Union[Path, str]] = ()):
        with self._work_with_proxy_script() as temp_dir_path:
            in_path, out_path = (
                Path(temp_dir_path).joinpath(f"{s}.json").resolve()
                for s in ["in", "out"]
                )
            sim_in_info = {
                'script_content': script_file.read(),
                'quiet': quiet,
                'custom_labware_paths': [
                    str(Path(x).resolve()) for x in custom_labware_paths],
                'out_path': str(out_path)
                }
            with in_path.open("w") as sim_in_f:
                json.dump(sim_in_info, sim_in_f)
            logger.info("Starting simulation")
            self._run_python(
                "run.py", "sim", in_path, cwd=temp_dir_path, quiet=False)
            with out_path.open("r") as sim_out_f:
                sim_out_info = json.load(sim_out_f)
        if sim_out_info['aborted']:
            logger.error("Protocol script aborted")
            return None, None, sim_out_info['traceback_lines']
        else:
            logger.info("Simulation finished")
        return sim_out_info['log_lines'], sim_out_info['est_duration_s'], None

    def _run_python(self, *args, cwd: Optional[Union[Path, str]] = None,
                    quiet: bool = False):
        python_path = self._get_bin_path("python")
        return (subprocess.check_call if quiet else subprocess.check_output)(
            [str(python_path)] + list(args),
            cwd=cwd,
            **(
                {
                    'stderr': subprocess.DEVNULL,
                    'stdout': subprocess.DEVNULL
                    } if quiet else {}
                )
            )

    def _sim_env_is_ready(self):
        try:
            self._run_python("-c", "import opentrons", quiet=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            return False
        return True

    def _build_sim_env_if_needed(self):
        if not self._sim_env_is_ready():
            logging.getLogger(__name__).info(
                "Initializing sim environment "
                "(this only neds to be done once)...") # FIXME: this doesn't actually get printed
            self.init_sim_env()

    def init_sim_env(self):
        builder = _OtSimEnvBuilder(extra_pkgs=EXTRA_PKGS)
        builder.create(self.env_dir)


class ProtocolScriptAborted(RuntimeError):
    traceback_lines: list[str]

    def __init__(self, traceback_lines: Iterable[str]):
        self.traceback_lines = list(traceback_lines)


class ProtocolAnalyzer:
    def __init__(self, log_lines, plate_slots: Sequence[Any] = (),
                 custom_labware_paths: Sequence[str] = (),
                 est_duration_s: Optional[float] = None):
        self.plate_slots = [str(x) for x in plate_slots]
        self.tip_touches = collections.defaultdict(set)
        self.tip_n_uses = collections.defaultdict(int)
        self.plate_tip_cols = {}
        self.rack_n_swaps = collections.defaultdict(int)
        self.n_steps = None
        self.tracker = PipettingTracker(
            custom_labware_paths=custom_labware_paths)
        self.est_duration_s = est_duration_s
        self.runlog_lines: list[str] = []
        self._process_run_log(log_lines)

    @classmethod
    def from_protocol_file(cls, prot_file, *args, add_vars=None,
                           prepend_text="", labware_paths=None,
                           quiet=False, **kwargs):
        if labware_paths is None:
            labware_paths = [accs_npf_paths.get_labware_defs_dir()]
        file_to_analyze = io.StringIO()
        file_to_analyze.write(
            prepend_text + "\nSIM_EMIT_TRACKING_EVENTS=True\n")
        if quiet:
            file_to_analyze.write("SIM_QUIET=True\n")
        if add_vars is not None:
            for key, value in add_vars.items():
                file_to_analyze.write(f"{key}={value}\n")
        file_to_analyze.write(prot_file.read())
        file_to_analyze.seek(0)

        sim_proxy = SimProxy()
        log_lines, est_duration_s, traceback_lines = sim_proxy.simulate(
            file_to_analyze, quiet=quiet, custom_labware_paths=labware_paths)
        if traceback_lines is not None:
            raise ProtocolScriptAborted(traceback_lines=traceback_lines)

        return cls(log_lines, custom_labware_paths=labware_paths,
                   est_duration_s=est_duration_s, **kwargs)

    def _handle_sim_event(self, event_name: str, args: Sequence):
        if event_name == "INIT_TIPRACK":
            slot_no, = args
            slot_name = str(slot_no)
            if slot_name in self.rack_n_swaps:
                self.rack_n_swaps[slot_name] += 1
            else:
                self.rack_n_swaps[slot_name] = 0
            self.tracker.process_inittiprack_event(slot_no)
        elif event_name == "SET_LABWARE":
            slot_no, def_name, name = args
            self.tracker.set_labware(slot_no, def_name, name)
        elif event_name == "DISPENSE":
            pipette_name, vol, slot_no, start_well_name, n_channels = args
            self.tracker.process_dispense_event(
                pipette_name, vol, slot_no, start_well_name, n_channels)
        elif event_name == "ASPIRATE":
            pipette_name, vol, slot_no, start_well_name, n_channels = args
            self.tracker.process_aspirate_event(
                pipette_name, vol, slot_no, start_well_name, n_channels)
        elif event_name == "ADD_LIQUID":
            slot_no, well_name, liquid_name, vol = args
            self.tracker.add_liquid(
                slot_no, well_name, liquid_name, vol)
        elif event_name == "PICKUP_TIPS":
            pipette_name, slot_no, start_well_name, n_channels = args
            self.tracker.process_pickuptips_event(
                pipette_name, slot_no, start_well_name, n_channels)
            slot_name = self.tracker.tipboxes[slot_no]
            self.tip_n_uses[(slot_name, start_well_name)] += 1
            # TODO: Absorb this functionality into the tracker class and do it smarter?
        elif event_name == "DROP_TIPS":
            pipette_name, = args
            self.tracker.process_droptips_event(pipette_name)
        else:
            raise ValueError(f"Unrecognized event {event_name}, args: {args}")

    def _process_run_log(self, log_lines: Iterable[str]):
        log_lines = list(log_lines)
        self.n_steps = len(log_lines)
        for log_line in log_lines:
            match = re.match(r"\!([A-Z_]+)\((.+)\)", log_line)
            if match:
                event_name = match.group(1)
                event_args = ast.literal_eval(f"[{match.group(2)}]")
                self._handle_sim_event(event_name, event_args)
                continue
            self.runlog_lines.append(log_line)

    def get_tip_wells_for_plate_wells(self, slot, well_names):
        # WARNING: PipettingTracker doesn't currently understand the concept
        # of swapped tip boxes!
        return [
            (str(tip_slot), tip_well)
            for (tip_slot, tip_well)
            in self.tracker.get_tip_locs_touching_wells(int(slot), well_names)
            ]
