from collections import OrderedDict
import copy
from dataclasses import dataclass
import datetime
import hashlib
import inspect
import json
import time
import io
import itertools
import logging
from pathlib import Path
import traceback
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple, Union

import cProfile
import numpy as np
import opentrons
from opentrons import protocol_api


def run(ot_ctx: protocol_api.ProtocolContext):
    ''' This is the entry point the Opentrons software calls into. Protocol
    steps for ACCS protocols go in ps_run() which we wrap here to add some
    logging functionality. '''
    for name in 'ps_class', 'ps_run', 'run_config':
        if name not in globals():
            raise RuntimeError(f"{name} definition missing from protocol file")
    global ps_class
    global ps_run
    global run_config

    p = ps_class(ot_ctx=ot_ctx, run_config=run_config)
    p.send_protocol_start_msg()

    try:
        if (
                p.get_run_param('logging', 'use_profiler', default=True)
                and not p.is_simulating()):
            p.log_debug("Running protocol in profiler")
            dump_path = p.get_log_path("pstat.dmp")
            if dump_path is not None:
                p.log_debug(f"Writing profiler data to: {str(dump_path)!r}")
            cProfile.runctx(
                "ps_run(p)",
                globals(),
                locals(),
                filename=p.get_log_path("pstat.dmp"))
        else:
            ps_run(p)
    except Exception as e:
        if e.__class__.__name__ in (
                'SmoothieAlarm', 'CancelledError', 'ExecutionCancelledError'):
            # Note: Matching by name so we fail gracefully if OT's
            # implementation changes
            p.log_notice("Protocol aborting due to manual cancel or "
                         "motion controller alarm")
            p.log_debug(f"Exception was: {e!r}")
        else:
            p.mark_finish_time(aborted=True)
            p.logger.exception(e)
            exc_desc = traceback.format_exc()
            p.log_error("Protocol aborting due to fatal error")
            p.log_debug(f"Traceback follows:\n{exc_desc}")
            try:
                p.write_json_record()
            except Exception:  # pylint: disable=broad-exception-caught
                pass  # oh well
        raise

    p.mark_finish_time()
    p.write_json_record()
    p.send_protocol_end_msg()


def get_logger(context: Union['ProtocolSteps', protocol_api.ProtocolContext],
               output_path: Optional[str] = None):
    ''' Set up the logging system (or a dummy stand-in if we're simulating)
    and return a logger object for the protocol script to use. '''
    if context.is_simulating():
        class SystemJournalHandler(logging.StreamHandler):
            def __init__(self):
                if globals().get('SIM_QUIET'):
                    stream = io.StringIO()
                else:
                    stream = globals().get('SIM_LOG_STREAM')
                super().__init__(stream)

            def format(self, record: logging.LogRecord):
                pri_word = getattr(record, 'pri_word', record.levelname)
                return f"{pri_word + ':':8s} {record.msg}"
    else:
        # This is in a conditional block since we don't want or need the
        # systemd stuff while simulating
        import systemd.journal

        class SystemJournalHandler(logging.Handler):
            def emit(self, record):
                priority = record.__dict__.get(
                    'systemd_priority', systemd.journal.LOG_INFO)
                systemd.journal.send(
                    self.format(record),
                    PRIORITY=priority,
                    LOGGER="opentrons.user_protocol")

    class ProtocolJournalLoggerAdapter:
        ''' We use the syslog priority level NOTICE specifically to signal
        to otlogbot that a message should get forwarded as a Slack alert,
        but the Python logging module doesn't have a .notice() '''
        def __init__(self, logger: logging.Logger):
            self._logger = logger

        def _log_with_systemd_priority(
                self, fn_name: str, pri_word: str, systemd_pri_name: str,
                msg: str, *args, **kwargs):
            if 'extra' not in kwargs:
                kwargs['extra'] = {}
            kwargs['extra']['pri_word'] = pri_word
            if not context.is_simulating():
                kwargs['extra']['systemd_priority'] = \
                    getattr(systemd.journal, systemd_pri_name)
            getattr(self._logger, fn_name)(msg, *args, **kwargs)

        def exception(self, msg: str, *args, **kwargs):
            self._log_with_systemd_priority(
                'exception', 'EXCEPTION', 'LOG_ERR', msg, *args, **kwargs)

        def error(self, msg: str, *args, **kwargs):
            self._log_with_systemd_priority(
                'error', 'ERROR', 'LOG_ERR', msg, *args, **kwargs)

        def warning(self, msg: str, *args, **kwargs):
            self._log_with_systemd_priority(
                'warning', 'WARNING', 'LOG_WARNING', msg, *args, **kwargs)

        def notice(self, msg: str, *args, **kwargs):
            self._log_with_systemd_priority(
                'warning', 'NOTICE', 'LOG_NOTICE', msg, *args, **kwargs)

        def info(self, msg: str, *args, **kwargs):
            self._log_with_systemd_priority(
                'info', 'INFO', 'LOG_INFO', msg, *args, **kwargs)

        def debug(self, msg: str, *args, **kwargs):
            self._log_with_systemd_priority(
                'debug', 'DEBUG', 'LOG_DEBUG', msg, *args, **kwargs)

        def __getattr__(self, name):
            return getattr(self._logger, name)

    class ProtocolJournalFormatter(logging.Formatter):
        def format(self, record: logging.LogRecord):
            pri_word = getattr(record, 'pri_word', record.levelname)
            ts_desc = datetime.datetime.fromtimestamp(record.created)\
                .isoformat(sep=" ", timespec='milliseconds')
            return f"{ts_desc} {pri_word + ':':8s} {record.msg}"

    logger = ProtocolJournalLoggerAdapter(logging.getLogger(__name__))
    logger.handlers.clear()
    logger.setLevel(logging.DEBUG)
    logger.addHandler(SystemJournalHandler())
    if output_path is not None:
        path_obj = Path(output_path).resolve()
        path_obj.parent.mkdir(parents=True, exist_ok=True)
        handler = logging.FileHandler(path_obj)
        handler.setFormatter(ProtocolJournalFormatter())
        logger.addHandler(handler)
    return logger


class RunConfigError(ValueError):
    pass


class TiprackProxy:
    ''' Wrapper class for intercepting method calls on tipracks. We don't
    actually use this for anything currently. '''
    def __init__(self, tip_rack: protocol_api.labware.Labware,
                 context: 'ProtocolSteps'):
        self._tip_rack = tip_rack
        self._context = context

    def __getattr__(self, name):
        attr = getattr(self._tip_rack, name)
        if callable(attr) and attr.__self__ is not None:
            fn = getattr(self._tip_rack.__class__, name)
            meth = fn.__get__(self, self.__class__)
            return meth
        return attr

    def __setattr__(self, name, value):
        if name in ('_tip_rack', '_context'):
            super().__setattr__(name, value)
        else:
            setattr(self._tip_rack, name, value)


class InstrumentProxy:
    ''' Wrapper class for InstrumentContext. ACCS protocols access the pipettes
    though this wrapper, which mostly mimics the normal API but adds hooks
    that make some special features work, mainly the tiprack refill alerts and
    passing events to the protocol analyzer during simulation.'''
    def __init__(self, instrument, context):
        self._instrument = instrument
        self._context = context
        self._last_location = None

    def deref_tiprack(self, slot_no: int):
        self.tip_racks[:] = [
            x for x in self.tip_racks if int(x.parent) != slot_no]

    def get_next_tip(self,
                     slot_nos: Optional[Iterable[int]] = None
                     ) -> Optional[opentrons.protocol_api.labware.Well]:
        next_tip = None
        for tip_rack in self.tip_racks:
            slot_no = int(tip_rack.parent)
            if slot_nos is not None and slot_no not in slot_nos:
                continue
            next_tip = tip_rack.next_tip()
            if next_tip is not None:
                break

        return next_tip

    def pick_up_tip(
        self,
        location: Optional[Union[opentrons.types.Location,
                                 protocol_api.labware.Well]] = None,
        *args,
        **kwargs
            ):
        if location is None:
            location = self.get_next_tip()

        if location is not None:
            self._context._before_pick_up_tip(self, location)

        self._instrument.pick_up_tip(location, *args, **kwargs)
        return self

    pick_up_tips = pick_up_tip

    def drop_tip(
        self,
        location: Optional[Union[opentrons.types.Location,
                                 protocol_api.labware.Well]] = None,
        *args,
        **kwargs
            ):
        if location is None:
            location = self._context.get_next_trash(self)
        self._context._before_drop_tip(self)
        self._instrument.drop_tip(location, *args, **kwargs)
        return self

    drop_tips = drop_tip

    def return_tip(self, *args, **kwargs):
        self._context._before_drop_tip(self)
        self._instrument.return_tip(*args, **kwargs)
        return self

    return_tips = return_tip

    def move_to(self, location: opentrons.types.Location, **kwargs):
        self._instrument.move_to(location, **kwargs)
        self._last_location = location

    def aspirate(
        self,
        volume: Optional[float] = None,
        location: Optional[Union[opentrons.types.Location,
                                 protocol_api.labware.Well]] = None,
        rate: float = 1.0,
        in_air: bool = False,
            ):
        start_vol = self._instrument.current_volume
        self._instrument.aspirate(volume=volume, location=location, rate=rate)
        aspirated_vol = self._instrument.current_volume - start_vol
        if location is not None:
            self._last_location = location
        if not in_air:
            # TODO: Is this the most appropriate way to handle this?
            self._context._after_aspirate(
                pipette=self,
                volume=aspirated_vol,
                location=self._last_location)
        return self

    def dispense(
        self,
        volume: Optional[float] = None,
        location: Optional[Union[opentrons.types.Location,
                                 protocol_api.labware.Well]] = None,
        rate: float = 1.0,
        in_air: bool = False
            ):
        start_vol = self._instrument.current_volume
        self._instrument.dispense(volume=volume, location=location, rate=rate)
        dispensed_vol = start_vol - self._instrument.current_volume
        if location is not None:
            self._last_location = location
        if not in_air:
            self._context._after_dispense(
                pipette=self,
                volume=dispensed_vol,
                location=self._last_location)
        return self

    def blow_out(
        self,
        location: Optional[Union[opentrons.types.Location,
                                 protocol_api.labware.Well]] = None
            ):
        start_vol = self._instrument.current_volume
        self._instrument.blow_out(location)
        if location is not None:
            self._last_location = location
        self._context._after_dispense(
            pipette=self,
            volume=start_vol,
            location=self._last_location)
        return self

    def __getattr__(self, name):
        attr = getattr(self._instrument, name)
        if callable(attr) and attr.__self__ is not None:
            fn = getattr(self._instrument.__class__, name)
            meth = fn.__get__(self, self.__class__)
            return meth
        return attr

    def __setattr__(self, name, value):
        if name in ('_instrument', '_context', '_last_location'):
            super().__setattr__(name, value)
        else:
            setattr(self._instrument, name, value)


PipetteRef = Union[InstrumentProxy, str]


@dataclass
class TiprackResetGroup:
    slot_nos: Tuple[int, ...]
    warning_done: bool = False


class ProtocolSteps:
    ''' This is the context object you interact with in the body of ps_run()
    in order to make the robot do things.
    '''
    PIPETTE_TYPE_RIGHT: Optional[str] = None
    PIPETTE_TYPE_LEFT: Optional[str] = None

    PIPETTE_ALIAS_LEFT = 'left'
    PIPETTE_ALIAS_RIGHT = 'right'

    NoDefault = object()

    def __init__(self, ot_ctx: protocol_api.ProtocolContext, run_config: dict):
        self._set_ot_ctx(ot_ctx)
        self.run_config = run_config
        self.pipette_left = None
        self.pipette_right = None
        pipette_type_left = self.get_run_param(
            'equipment', f'pipette_type_{self.PIPETTE_ALIAS_LEFT}',
            default=self.PIPETTE_TYPE_LEFT)
        pipette_type_right = self.get_run_param(
            'equipment', f'pipette_type_{self.PIPETTE_ALIAS_RIGHT}',
            default=self.PIPETTE_TYPE_RIGHT)
        if pipette_type_left is not None:
            self.pipette_left = InstrumentProxy(
                ot_ctx.load_instrument(pipette_type_left, 'left'), self)
            if self.PIPETTE_ALIAS_LEFT != 'left':
                setattr(self, f'pipette_{self.PIPETTE_ALIAS_LEFT}',
                        self.pipette_left)
        if pipette_type_right is not None:
            self.pipette_right = InstrumentProxy(
                ot_ctx.load_instrument(pipette_type_right, 'right'), self)
            if self.PIPETTE_ALIAS_RIGHT != 'right':
                setattr(self, f'pipette_{self.PIPETTE_ALIAS_RIGHT}',
                        self.pipette_right)

        self._init_logging()

        self.tipracks: Dict[int, TiprackProxy] = OrderedDict()
        self.labware: Dict[int, protocol_api.labware.Labware] = {}
        self.modules: Dict[int, protocol_api.context.ModuleContext] = {}
        self._tiprack_reset_queues: Dict[str, List[TiprackResetGroup]] = {
            self.PIPETTE_ALIAS_LEFT: [],
            self.PIPETTE_ALIAS_RIGHT: []
        }
        self._pipette_alias_picking_up_tips: Optional[PipetteRef] = None
        self._init_trash_locations()

    def _set_ot_ctx(self, ot_ctx: protocol_api.ProtocolContext):
        self.ot_ctx = ot_ctx
        self.is_simulating = ot_ctx.is_simulating
        self.delay = ot_ctx.delay

    def _init_log_dir(self):
        self.log_dir = (
            globals().get('SIM_LOG_DIR',
                          self.get_run_param('logging',
                                             'log_dir', default=None))
            if globals().get('SIM_WRITE_LOGFILE') or not self.is_simulating()
            else None)

    def _init_run_id(self):
        run_id = self.get_run_param('logging', 'run_id', default=None)
        run_id = run_id if run_id is not None else "norunid"
        self.run_id = run_id
        date_desc = datetime.datetime.now().strftime("%y%m%d-%H%M%S")
        expanded_run_id = f"{date_desc}-{run_id}"
        script_contents_hash = globals().get('SCRIPT_CONTENTS_HASH')
        if script_contents_hash:
            expanded_run_id += "-" + script_contents_hash[:7]
        self.expanded_run_id = expanded_run_id

    def get_log_path(self, ending: str) -> Optional[Path]:
        return (
            Path(self.log_dir).joinpath(
                f"{self.expanded_run_id}.{ending}")
            if self.log_dir is not None
            else None)

    def update_results(self, key: str, new_info: dict, write_now: bool = True):
        ''' Add info to the results record to be dumped out to to the JSON
        end of the protocol. Keys will be converted to strings and values must
        be JSON-serializable. If write_now is true, the file will be written
        out immediately.
        '''
        new_info = {str(k): v for (k, v) in new_info.items()}
        if key not in self.json_rec_content['results']:
            self.json_rec_content['results'][str(key)] = {}
        self.json_rec_content['results'][key].update(new_info)

    def write_json_record(self):
        ''' Write out the JSON results file immediately. The outer wrapper code
        will attempt to do this on exit even in the case of an exception, so
        you don't need to call this, but if you want to write the file out
        early you can.
        '''
        json_path = self.get_log_path("run_info.json")
        if json_path is None:
            return
        with json_path.open("w") as f:
            json.dump(self.json_rec_content, f, indent=4)

    def mark_finish_time(self, aborted: bool = False):
        self.json_rec_content['aborted'] = aborted
        self.json_rec_content['finish_time'
                              ] = datetime.datetime.utcnow().isoformat()

    def get_robot_id(self) -> Optional[str]:
        if self.is_simulating():
            return "SIMTRON9000"
        try:
            with Path("/var/serial").open() as f:
                return f.read().strip()
        except IOError:
            return None

    def _init_logging(self):
        hash_key_desc = {
            'SOURCE_HASH': "source hash",
            'CONFIG_HASH': "config hash",
            'SCRIPT_CONTENTS_HASH': "script hash",
            }
        pipette_info_key_desc = {
            'pipette_id': "serial number",
            }

        self._init_log_dir()
        self._init_run_id()

        log_path = self.get_log_path("run_log.txt")
        contents_path = self.get_log_path("script_contents.txt")
        script_contents = globals().get('SCRIPT_CONTENTS')

        robot_id = self.get_robot_id()

        self.json_rec_content = {
            'run_id': self.expanded_run_id,
            'script_hashes': {
                key.lower(): globals().get(key) for key in hash_key_desc
                },
            'start_time': datetime.datetime.utcnow().isoformat(),
            'finish_time': None,
            'aborted': None,
            'robot_id': robot_id,
            'pipettes': {},
            'ref_info': self.get_run_param('logging', 'run_info'),
            'results': {},
            }

        self.logger = get_logger(self.ot_ctx, output_path=log_path)
        self.log_debug = self.logger.debug
        self.log_info = self.logger.info
        self.log_notice = self.logger.notice
        self.log_warning = self.logger.warning
        self.log_error = self.logger.error
        self.log_debug("(start of logging session)")
        if log_path is not None:
            self.log_debug(f"Writing log hardcopy to path: {str(log_path)!r}")

        if (contents_path is not None) and script_contents:
            self.log_debug(
                f"Copying script contents to path: {str(contents_path)!r}")
            with contents_path.open("w", encoding=TEXT_ENCODING) as outf:
                outf.write(script_contents)

        run_info_items = [('run ID', self.expanded_run_id)]
        run_info_items += [(desc, globals().get(key))
                           for (key, desc) in hash_key_desc.items()]
        run_info_items += [(repr(k), v) for (k, v) in
                           self.get_run_param('logging', 'run_info').items()]
        for (k, v) in run_info_items:
            self.log_info(
                f"Run info: {hash_key_desc.get(k, k)}: "
                + (repr(v) if v is not None else '(missing)'))

        if robot_id:
            self.log_info(f"Robot info: name: {robot_id}")
        for pipette in (self.pipette_left, self.pipette_right):
            if pipette is None:
                continue
            pipette_alias = self._get_pipette_alias(pipette)
            self.json_rec_content['pipettes'][pipette_alias] = {
                'mount': pipette.mount
                }
            for key in ('model', 'pipette_id'):
                if key not in pipette.hw_pipette:
                    self.json_rec_content['pipettes'][
                        pipette_alias][key] = None
                    continue
                self.json_rec_content[key] = pipette.hw_pipette[key]
                self.log_info(
                    f"Pipette info ({pipette_alias})({pipette.mount} mount): "
                    f"{pipette_info_key_desc.get(key, key)}: "
                    f"{pipette.hw_pipette[key]!r}")
        self.write_json_record()

    def _init_trash_locations(self):
        trash_well = self.ot_ctx.fixed_trash.wells()[0]
        trash_center = trash_well.center().point
        right_locs = [
            opentrons.types.Location(
                trash_center + opentrons.types.Point(x, 0., 0.), trash_well)
            for x in np.linspace(-50., 65., 24)
            ]
        left_locs = [
            opentrons.types.Location(
                trash_center + opentrons.types.Point(x, 0., 0.), trash_well)
            for x in np.linspace(-40., 30., 15)
            ]
        self.trash_locations = {
            self.PIPETTE_ALIAS_LEFT: left_locs,
            self.PIPETTE_ALIAS_RIGHT: right_locs
            }
        self.trash_cycles = {
            k: itertools.cycle(v) for (k, v) in self.trash_locations.items()}

    def _emit_sim_event(self, event_name: str, args: Iterable):
        if not globals().get('SIM_EMIT_TRACKING_EVENTS', False):
            return
        args_str = ",".join(repr(x) for x in args) if args else None
        self.ot_ctx.comment(f"!{event_name}({args_str})")

    def _sim_prefill_labware(self, slot_no: int, name: str):
        initial_fills = self.get_run_param(
            'liquid_tracking', 'initial_fills', name, default=())
        for (well_name, liquid_name, vol) in initial_fills:
            self.sim_add_liquid(slot_no, (well_name,), liquid_name, vol)

    def _filter_tiprack_slot_nos(
            self, pipette: PipetteRef, slot_nos: Iterable[int]) \
            -> Iterable[int]:
        pipette = self.match_pipette_ref(pipette)[1]
        relevant = tuple(int(rack.parent) for rack in pipette.tip_racks)
        return (x for x in slot_nos if x in relevant)

    def _get_pipette_alias(self, ref: PipetteRef):
        if isinstance(ref, str):
            return ref
        elif ref is self.pipette_left:
            return self.PIPETTE_ALIAS_LEFT
        elif ref is self.pipette_right:
            return self.PIPETTE_ALIAS_RIGHT

    def _resolve_pipette_alias(self, alias: str) -> InstrumentProxy:
        valid_options = []
        pipette = False
        for o in [self] + list(self.__class__.__mro__):
            if not hasattr(o, "PIPETTE_ALIAS_LEFT") \
                    or not hasattr(o, "PIPETTE_ALIAS_RIGHT"):
                break
            d = {
                o.PIPETTE_ALIAS_LEFT: self.pipette_left,
                o.PIPETTE_ALIAS_RIGHT: self.pipette_right
                }
            valid_options += list(k for k in d.keys()
                                  if k not in valid_options)
            try:
                pipette = d[alias]
                break
            except KeyError:
                continue
        if pipette is False:
            raise KeyError(
                f"Invalid pipette alias {alias!r} (valid options: "
                + ", ".join(repr(k) for k in d.keys())
                + ")")
        elif pipette is None:
            raise KeyError(f"No pipette configured for alias {alias!r}")
        return pipette

    def _assert_tiprack_present(self, slot_no: int):
        if slot_no not in self.tipracks:
            raise KeyError(f"No tiprack on slot {slot_no}")

    def _assert_labware_present(self, slot_no: int):
        if slot_no not in self.labware:
            raise KeyError(f"No labware on slot {slot_no}")

    def _resolve_well_location(
        self,
        slot_no: Optional[int] = None,
        labware: Optional[protocol_api.labware.Labware] = None,
        well_name: Optional[str] = None
            ):
        if slot_no is not None or labware is not None:
            if well_name is None:
                raise ValueError(
                    "well_name is required with slot_no or labware")
        elif well_name is not None:
            raise ValueError("slot_no or labware required with well_name")
        else:
            return None
        if labware is None:
            self._assert_labware_present(slot_no)
            labware = self.labware[slot_no]
        return labware.well(well_name)

    def _location_to_slot_and_well_name(
        self,
        location: Optional[Union[opentrons.types.Location,
                           protocol_api.labware.Well]]
            ) -> Tuple[Optional[int], Optional[str]]:
        if location is None:
            return None, None
        if isinstance(location, opentrons.types.Location):
            labware, well = location.labware.get_parent_labware_and_well()
        elif isinstance(location, protocol_api.labware.Well):
            well = location
            labware = well.parent
        else:
            raise TypeError(f"Unsupported type for location: {type(location)}")
        slot_no = self.get_slot_no(labware)
        well_name = well.well_name
        return slot_no, well_name

    def _before_pick_up_or_drop_tip(self, pipette: PipetteRef):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        self._pipette_alias_picking_up_tips = pipette_alias
        reset_queue = self._tiprack_reset_queues[pipette_alias]
        if reset_queue:
            slot_nos = reset_queue[0].slot_nos
            if not reset_queue[0].warning_done \
                    and not pipette.get_next_tip(slot_nos):
                self.send_tiprack_reset_alert(slot_nos)
                reset_queue[0].warning_done = True
            if not pipette.get_next_tip():
                self.reset_tipracks(slot_nos)
                reset_queue.pop(0)

    def _before_pick_up_tip(
            self,
            pipette: PipetteRef,
            location: Union[opentrons.types.Location,
                            protocol_api.labware.Well]
            ):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        self._before_pick_up_or_drop_tip(pipette)
        if location is None:
            location = pipette.get_next_tip()
        slot_no, start_well_name = \
            self._location_to_slot_and_well_name(location)
        self._emit_sim_event(
            "PICKUP_TIPS",
            (pipette_alias, slot_no, start_well_name, pipette.channels))

    def _before_drop_tip(self, pipette: PipetteRef):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        self._before_pick_up_or_drop_tip(pipette)
        self._emit_sim_event("DROP_TIPS", (pipette_alias,))

    def _after_aspirate(
        self,
        pipette: PipetteRef,
        volume: float,
        location: Optional[Union[opentrons.types.Location,
                           protocol_api.labware.Well]]
            ):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        slot_no, well_name = self._location_to_slot_and_well_name(location)
        evt_info = (
            pipette_alias, volume, slot_no, well_name, pipette.channels)
        self._emit_sim_event("ASPIRATE", evt_info)
        return evt_info

    def _after_dispense(
        self,
        pipette: PipetteRef,
        volume: float,
        location: Optional[Union[opentrons.types.Location,
                           protocol_api.labware.Well]]
            ):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        slot_no, well_name = self._location_to_slot_and_well_name(location)
        self._emit_sim_event(
            "DISPENSE",
            (pipette_alias, volume, slot_no, well_name, pipette.channels))

    def _aspirate_or_dispense(
        self,
        meth_name: str,
        pipette: PipetteRef,
        vol: Optional[float] = None,
        location: Optional[Union[opentrons.types.Location,
                           protocol_api.labware.Well]] = None,
        rate: float = 1.0,
        slot_no: Optional[int] = None,
        labware: Optional[protocol_api.labware.Labware] = None,
        well_name: Optional[str] = None,
        in_air: bool = False,
        flow_rate: Optional[float] = None
            ):
        if location is None:
            location = self._resolve_well_location(
                slot_no=slot_no, labware=labware, well_name=well_name)
        pipette = self.match_pipette_ref(pipette)[1]
        if flow_rate is not None:
            old_flow_rate = getattr(pipette.flow_rate, meth_name)
            setattr(pipette.flow_rate, meth_name, flow_rate)
        getattr(pipette, meth_name)(vol, location, rate, in_air=in_air)
        if flow_rate is not None:
            setattr(pipette.flow_rate, meth_name, old_flow_rate)
        return pipette

    def send_protocol_start_msg(self):
        global metadata
        protocol_name = metadata.get('protocolName')
        name_part = f": {protocol_name}" if protocol_name else ""
        self.log_notice(f"Protocol starting{name_part} "
                        f"({self.expanded_run_id})")

    def send_protocol_end_msg(self):
        self.log_notice("Protocol complete.")

    def get_run_param(self, *keys: str, default: Any = NoDefault):
        ''' Retrieve a configuration value from the run_config '''
        ref = self.run_config
        for key in keys:
            if key not in ref:
                if default is not ProtocolSteps.NoDefault:
                    return default
                desc = "run_config" + "".join(f"['{x}']" for x in keys)
                raise RunConfigError(
                    f"Missing required config parameter: {desc}")
            ref = ref[key]
        return copy.deepcopy(ref)

    def comment(self, msg: str, also_log: bool = False):
        ''' Wrapper for ProtocolContext.comment(), adds a comment to the run
        log at simulation time with the option to also emit at run time.'''
        self.ot_ctx.comment(msg)
        if also_log:
            self.log_info(msg)

    def pause_with_message(self, msg: str):
        ''' Pause the protocol and display a message in the Opentrons app.
        If you have ot2logbot running this will also trigger a Slack alert.
        '''
        self.log_notice(f"Protocol paused: {msg}")
        self.ot_ctx.pause(msg)
        self.delay(0.1)  # Trigger wait for run flag
        self.log_notice("Protocol resumed.")

    def send_tiprack_reset_alert(self, slot_nos: Iterable[int]):
        slot_nos = tuple(slot_nos)
        s_or_not = "s" if len(slot_nos) > 1 else ""
        self.log_notice(
            f"Replace tiprack{s_or_not} on slot{s_or_not} "
            + ", ".join(str(x) for x in sorted(slot_nos)))

    def match_pipette_ref(self, ref: PipetteRef
                          ) -> Tuple[str, InstrumentProxy]:
        ''' Take a value that can be either a pipette nickname or an
        InstrumentProxy reference and return both of those things.
        '''
        if isinstance(ref, InstrumentProxy):
            return self._get_pipette_alias(ref), ref
        elif isinstance(ref, str):
            return ref, self._resolve_pipette_alias(ref)
        raise TypeError(f"Pipette reference not understood: {ref!r}")

    def get_slot_no(self, labware: protocol_api.labware.Labware):
        ''' Returns the number of the deck slot a given piece of labware is
        installed in '''
        parent = labware
        while hasattr(parent, "parent"):
            parent = parent.parent
        return int(parent)

    def load_tiprack(self, slot_no: int, pipette: PipetteRef,
                     def_name: Optional[str] = None):
        '''Assign a tiprack to a given deck slot and pipette. If no labware
        definition name is specified, it will be controlled by the run_config.
        '''
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        if def_name is None:
            def_name = self.get_run_param(
                'equipment', f'tiprack_def_{pipette_alias}')
        if self.slot_is_occupied(slot_no):
            raise ValueError(f"slot {slot_no} already occupied")
        tiprack = TiprackProxy(
            self.ot_ctx.load_labware_by_name(def_name, slot_no), self)
        self.tipracks[slot_no] = tiprack
        pipette.tip_racks.append(tiprack)
        self._emit_sim_event("SET_LABWARE", (slot_no, def_name, None))
        self._emit_sim_event("INIT_TIPRACK", (slot_no,))
        self.log_debug(f"Tiprack of type '{def_name}' loaded on slot "
                       f"{slot_no} for pipette '{pipette_alias}'")
        return tiprack

    def load_labware(
        self, def_name: str, slot_no: Optional[int] = None,
        parent_module: Optional[protocol_api.contexts.ModuleContext] = None,
        name: Optional[str] = None
            ) -> protocol_api.labware.Labware:
        ''' Assign a labware item to a given deck slot. '''
        if parent_module is not None:
            labware = parent_module.load_labware_by_name(def_name)
            slot_no = int(parent_module.labware.parent.parent)
        elif slot_no is None:
            raise ValueError(
                "must specify slot_no or parent_module for load_labware()")
        elif self.slot_is_occupied(slot_no):
            raise ValueError(f"slot {slot_no} already occupied")
        else:
            labware = self.ot_ctx.load_labware_by_name(def_name, slot_no)
        self.labware[slot_no] = labware
        self._emit_sim_event("SET_LABWARE", (slot_no, def_name, name))
        if name is not None:
            self._sim_prefill_labware(slot_no, name)
        self.log_debug(f"Labware of type '{def_name}' loaded on "
                       f"slot {slot_no}")
        return labware

    def load_module(self, slot_no: int, def_name: str):
        ''' Assign a Module (e.g. tempdeck) to a given deck slot. '''
        if self.slot_is_occupied(slot_no):
            raise ValueError(f"slot {slot_no} already occupied")
        module = self.ot_ctx.load_module(def_name, slot_no)
        self.modules[slot_no] = module
        return module

    def load_tipracks(self, slot_nos: Iterable[int], pipette: PipetteRef,
                      def_name: Optional[str] = None):
        ''' Convenience method to load a bunch of tipracks at once. '''
        return {
            slot_no: self.load_tiprack(
                slot_no=slot_no,
                def_name=def_name,
                pipette=pipette)
            for slot_no in slot_nos}

    def reset_tipracks(self, slot_nos: Iterable[int]):
        ''' Convenience method to reset multiple tipracks '''
        for slot_no in slot_nos:
            self.reset_tiprack(slot_no)

    def reset_tiprack(self, slot_no: int):
        ''' Reset the tiprack on the specific tiprack to full. '''
        self._assert_tiprack_present(slot_no)
        self.tipracks[slot_no].reset()
        self.log_debug(f"Tiprack on slot {slot_no} reset")
        self._emit_sim_event("INIT_TIPRACK", (slot_no,))

    def unload_tiprack(self, slot_no: int):
        ''' Remove the tiprack on the specified slot. This is intended for
        situations where the deck layout needs to be changd partway through a
        protocol. '''
        del self.ot_ctx.deck[self.tipracks[slot_no].parent]
        del self.tipracks[slot_no]
        for pipette in self.pipette_left, self.pipette_right:
            pipette.deref_tiprack(slot_no=slot_no)

    def unload_tipracks(self, slot_nos: List[int]):
        ''' Convenience method to remove tipracks from multiple deck slots. '''
        for slot_no in slot_nos:
            self.unload_tiprack(slot_no)

    def arm_tiprack_reset(self, slot_nos: Iterable[int]):
        ''' Mark the tipracks on the specified slots to be auto-reset once
        there are no tips left on the deck '''
        for pipette_alias in self._tiprack_reset_queues:
            slot_nos_sub = \
                tuple(self._filter_tiprack_slot_nos(pipette_alias, slot_nos))
            if not slot_nos_sub:
                continue
            entry = TiprackResetGroup(tuple(slot_nos_sub))
            self._tiprack_reset_queues[pipette_alias].append(entry)

    def sim_add_liquid(self, slot_no: int, well_names: Sequence[str],
                       liquid_name: str, vol: float):
        # TODO: Also support Wells and Locations
        for well_name in well_names:
            self._emit_sim_event(
                "ADD_LIQUID", (slot_no, well_name, liquid_name, vol))

    def get_next_trash(self, pipette: PipetteRef):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        return next(self.trash_cycles[pipette_alias])

    def slot_is_occupied(self, slot_no: int):
        ''' Check whether there is somehting'''
        for d in (self.labware, self.tipracks, self.modules):
            if slot_no in d:
                return True

    def move_to(
        self,
        pipette: PipetteRef,
        location: Union[opentrons.types.Location, protocol_api.labware.Well]
            ):
        pipette = self.match_pipette_ref(pipette)[1]
        pipette.move_to(location)
        # TODO should there be a return value?

    def aspirate(self, *args, **kwargs):
        return self._aspirate_or_dispense('aspirate', *args, **kwargs)
    aspirate.__annotations__ = _aspirate_or_dispense.__annotations__

    def dispense(self, *args, **kwargs):
        return self._aspirate_or_dispense('dispense', *args, **kwargs)
    dispense.__annotations__ = _aspirate_or_dispense.__annotations__

    def blow_out(
        self,
        pipette: PipetteRef,
        location: Optional[Union[opentrons.types.Location,
                           protocol_api.labware.Well]] = None,
        slot_no: Optional[int] = None,
        labware: Optional[protocol_api.labware.Labware] = None,
        well_name: Optional[str] = None
            ):
        if location is None:
            location = self._resolve_well_location(
                slot_no=slot_no, labware=labware, well_name=well_name)
        pipette = self.match_pipette_ref(pipette)[1]
        return pipette.blow_out(location)

    def pick_up_tip(
        self,
        pipette: PipetteRef,
        location: Optional[Union[opentrons.types.Location,
                           protocol_api.labware.Well]] = None,
        slot_no: Optional[int] = None,
        well_name: Optional[str] = None
            ):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        if location is None and slot_no is not None:
            self._assert_tiprack_present(slot_no)
            if well_name is None:
                location = self.tipracks[slot_no].next_tip(
                    num_tips=pipette.channels)
                if location is None:
                    raise RuntimeError(
                        f"No tips available for pipette {pipette_alias} "
                        f"in tiprack on slot {slot_no}")
            else:
                location = self.tipracks[slot_no].well(well_name)
        pipette.pick_up_tip(location)

    pick_up_tips = pick_up_tip

    def drop_tip(self, pipette: PipetteRef, *args, **kwargs):
        pipette = self.match_pipette_ref(pipette)[1]
        pipette.drop_tip(*args, **kwargs)

    drop_tips = drop_tip


ps_class = ProtocolSteps


def _get_script_contents():
    target_frame = None
    frame = inspect.currentframe()
    while True:
        if frame.f_code.co_name == "run_python":
            target_frame = frame
            break
        frame = frame.f_back
        if frame is None:
            break
    return target_frame.f_locals['proto'].text


def _get_script_contents_hash():
    hash_obj = hashlib.sha1()
    hash_obj.update(_get_script_contents().encode(TEXT_ENCODING))
    return hash_obj.hexdigest()


try:
    SCRIPT_CONTENTS_HASH = _get_script_contents_hash()
    SCRIPT_CONTENTS = _get_script_contents()
except Exception:
    pass
