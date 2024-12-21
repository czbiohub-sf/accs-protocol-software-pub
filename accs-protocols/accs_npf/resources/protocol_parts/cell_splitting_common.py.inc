# Common files

import asyncio
from collections import OrderedDict
from dataclasses import dataclass
from enum import Enum
import inspect
import itertools
import time
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple, Union

from opentrons import protocol_api


@dataclass
class CellSplittingColSet:
    col_nos: Tuple[int, ...]
    trypsin_start_time: Optional[float] = None
    quenched: bool = False


class CellSplitting(ProtocolSteps):
    PIPETTE_TYPE_LEFT = 'p300_single_gen2'
    PIPETTE_TYPE_RIGHT = 'p300_multi_gen2'
    PIPETTE_ALIAS_1CH = '1ch'
    PIPETTE_ALIAS_8CH = '8ch'
    PIPETTE_ALIAS_LEFT = PIPETTE_ALIAS_1CH
    PIPETTE_ALIAS_RIGHT = PIPETTE_ALIAS_8CH

    class Liquid(Enum):
        WASTE = 'waste'
        PBS = 'pbs'
        TRYPSIN = 'trypsin'
        MEDIA = 'media'
        WATER = 'water'
        DETERGENT = 'detergent'
        CELLS = 'cells'

    class FlushStage(Enum):
        PRIME = 'prime'
        BETWEEN = 'between'
        WASH = 'wash'
        RINSE = 'rinse'

    class TempdeckStage(Enum):
        WARM = 'warm'
        COAST = 'coast'
        RT = 'rt'

    class TiltedMixStage(Enum):
        POSTQUENCH = 'postquench'
        PRETRANSFER = 'pretransfer'
        FROMSUSP = 'fromsusp'

    class FlatMixStage(Enum):
        PRETRANSFER = 'pretransfer'
        POSTTRANSFER = 'posttransfer'

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.pipette_1ch = self._resolve_pipette_alias(self.PIPETTE_ALIAS_1CH)
        self.pipette_8ch = self._resolve_pipette_alias(self.PIPETTE_ALIAS_8CH)
        self.init_labware()
        self._init_trough_wells()
        self.init_col_sets(
            cols_per_set=self.get_splitting_param('n_cols_per_set'))

    def init_labware(self):
        ''' Install the base labware loadout common for Cell Splitting
        protocols'''
        self.trough = self.load_labware(
            slot_no=self.get_splitting_param('trough_slot'),
            def_name=self.get_splitting_param('trough_def'),
            name='trough')
        self.tempdeck = self.load_module(
            slot_no=self.get_splitting_param('tempdeck_slot'),
            def_name=self.get_splitting_param('tempdeck_def'))
        self.source_plate = self.load_labware(
            parent_module=self.tempdeck,
            def_name=self.get_splitting_param('source_plate_def'),
            name='source_plate')
        self.dest_plates = OrderedDict()
        for plate_name in self.get_splitting_param('dest_plates'):
            self.dest_plates[plate_name] = self.load_labware(
                self.get_splitting_param('dest_plate_defs', plate_name),
                self.get_splitting_param('dest_plate_slots', plate_name),
                name=plate_name)

    def init_col_sets(self, cols_per_set: int):
        all_col_nos = list(self.get_col_nos())
        self.col_sets = []
        while all_col_nos:
            col_nos: List[int] = []
            for i in range(cols_per_set):
                if not all_col_nos:
                    break
                col_nos.append(all_col_nos.pop(0))
            if col_nos:
                self.col_sets.append(
                    CellSplittingColSet(col_nos=tuple(col_nos)))

    def _init_trough_wells(self):
        trough_config = self.get_splitting_param('trough_wells')
        self.trough_wells = {}
        self.trough_fixed_cycles = {}
        for liquid in self.Liquid:
            if liquid.value not in trough_config:
                continue
            self.trough_wells[liquid.value] = [
                self.trough.wells_by_name()[well_name]
                for well_name in trough_config[liquid.value]]
            if liquid in [self.Liquid.WASTE,]:
                self.trough_fixed_cycles[liquid.value] = itertools.cycle(self.trough_wells[liquid.value])
        self.trough_withdrawn_vol = {
            well_name: 0 for well_name in self.trough.wells_by_name().keys()}

    def _after_aspirate(
        self,
        pipette: PipetteRef,
        volume: float,
        location: Optional[Union[opentrons.types.Location,
                           protocol_api.labware.Well]]
            ):
        evt_info = super()._after_aspirate(
            pipette=pipette, volume=volume, location=location)
        pipette_alias, volume, slot_no, well_name, n_channels = evt_info
        if slot_no == self.get_slot_no(self.trough):
            self.trough_withdrawn_vol[well_name] += volume * n_channels

    def _get_dest_plate_col_info(
            self, dest_name: str, col_nos: Optional[Iterable[int]] = None) \
            -> Tuple[Tuple[int, ...], str]:
        all_col_nos = tuple(
            int(x)
            for x in self.dest_plates[dest_name].columns_by_name().keys())
        if col_nos is None:
            col_nos = self.get_col_nos()
        col_nos = tuple(col_nos)
        if set(col_nos) == set(all_col_nos):
            cols_desc = "all wells"
        else:
            cols_desc = "columns " + ', '.join(str(x) for x in col_nos)
        return col_nos, cols_desc

    def _get_dest_plate_name(self, slot_no: int) -> str:
        plates_by_slot = self.get_splitting_param('dest_plate_slots')
        try:
            return {plates_by_slot[k]: k for k in plates_by_slot}[slot_no]
        except KeyError:
            raise ValueError(
                f"no destination plate assigned to slot {slot_no}")

    def load_tiprack(self, slot_no: int, pipette: PipetteRef,
                     def_name: Optional[str] = None
                     ) -> protocol_api.labware.Labware:
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        if def_name is None:
            def_name = self.run_config['cell_splitting'][
                f'tiprack_def_{pipette_alias}']
        return ProtocolSteps.load_tiprack(self, slot_no, pipette, def_name)

    def get_splitting_param(self, *keys: str,
                            default: Any = ProtocolSteps.NoDefault):
        return self.get_run_param('cell_splitting', *keys, default=default)

    def get_col_nos(self, set_idx: Optional[int] = None) -> Sequence[int]:
        if set_idx is not None:
            return self.col_sets[set_idx].col_nos
        return range(
            self.get_splitting_param('start_col'),
            self.get_splitting_param('end_col') + 1)

    def col_set_exists(self, set_idx: int) -> bool:
        return set_idx < len(self.col_sets)

    def col_set_quench_ready(self, set_idx: int) -> bool:
        if (not self.col_set_exists(set_idx)) \
                or self.col_set_quenched(set_idx) \
                or self.col_sets[set_idx].trypsin_start_time is None:
            return False
        if self.is_simulating() \
                and globals().get('SIM_SKIP_WAITS', True):
            return True
        elapsed = time.monotonic() - self.col_sets[set_idx].trypsin_start_time
        return elapsed >= self.get_splitting_param('trypsin_time')

    def col_set_quenched(self, set_idx: int) -> bool:
        return self.col_set_exists(set_idx) and self.col_sets[set_idx].quenched

    def get_source_well_names(self, col_nos: Optional[Iterable[int]] = None,
                              include: Optional[Iterable[str]] = None):
        if col_nos is None:
            col_nos = self.source_plate.columns_by_name().keys()
        if include is not None:
            include = set(include)
        return [
            well.well_name
            for col_no in col_nos
            for well in self.source_plate.columns_by_name()[col_no]
            if include is None or well.well_name in include]

    def get_source_well_approach_loc(self, well_name: str):
        return self.get_source_well_bottom_center_loc(well_name) \
            .move(opentrons.types.Point(0., 0., 11.))
        # TODO: Verify that this is OK in the context of both the "aspirate
        # old media" and "pbs wash" steps

    def get_source_well_top_loc(self, well_name: str):
        well = self.source_plate.wells_by_name()[well_name]
        return well.top()

    def get_source_well_wall_loc(self, well_name: str):
        well = self.source_plate.wells_by_name()[well_name]
        return well.center().move(opentrons.types.Point(x=-3.4, y=0., z=2.))

    def get_source_well_bottom_corner_loc(self, well_name: str):
        well = self.source_plate.wells_by_name()[well_name]
        return well.center().move(opentrons.types.Point(x=-1.5, y=0., z=-5.))

    def get_source_well_bottom_center_loc(self, well_name: str):
        well = self.source_plate.wells_by_name()[well_name]
        return well.center().move(opentrons.types.Point(x=1.5, y=0., z=-4.))

    def get_tip_capacity(self, pipette: PipetteRef):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        return self.get_splitting_param(f'tip_capacity_{pipette_alias}')

    def get_trypsin_vol(self, well_name: str):
        # This is here in case it needs to be overriden on a per-well basis
        return self.get_splitting_param('trypsin_vol')

    def get_quench_vol(self, well_name: str):
        # This is here in case it needs to be overriden on a per-well basis
        return self.get_splitting_param('quench_vol')

    def get_postquench_vol(self, well_name: str):
        return self.get_trypsin_vol(well_name) + self.get_quench_vol(well_name)

    def get_presusp_vol(self, well_name: str):
        return self.get_splitting_param('presusp_well_vol')

    def get_pretransfer_vol(self, well_name: str, presusp: bool = False,
                            cci: bool = True):
        start_vol = (self.get_postquench_vol(well_name) if not presusp
                     else self.get_presusp_vol(well_name))
        return (start_vol
                - (self.get_splitting_param('cci_loading_vol') if cci else 0.))

    def dest_plate_exists(self, dest_name: str):
        return dest_name in self.dest_plates and (
            dest_name in self.get_splitting_param('dest_target_vols')
            or dest_name in self.get_splitting_param('target_counts',
                                                     default={})
            or dest_name in self.get_splitting_param('target_split_factors',
                                                     default={}))

    def get_dupe_prefill_vol(self, from_name: str,
                             dupe_plates: Iterable[str] = ()):
        dupe_plates = tuple(dupe_plates)
        target_vol_stock = \
            self.get_splitting_param('dest_target_vols', from_name)
        dupe_vols = {
            dupe_name: self.get_splitting_param('dest_target_vols', dupe_name)
            for dupe_name in dupe_plates}
        target_vol_dupes = sum(dupe_vols.values())
        target_vol_total = target_vol_stock + target_vol_dupes
        max_vol_8ch = self.get_tip_capacity(self.pipette_8ch)
        max_vol_1ch = self.get_tip_capacity(self.pipette_1ch)
        max_vol_stock = self.get_splitting_param(
            'plate_vol_limits', from_name)

        plates_desc = ", ".join(
            repr(name) for name in (from_name,) + dupe_plates)
        if target_vol_total > max_vol_stock:
            raise RunConfigError(
                f"Combined target volumes for {plates_desc} must be <= "
                f"{max_vol_stock:.1f} μL ({from_name!r} capacity)")
        for dupe_name, dupe_vol in dupe_vols.items():
            if dupe_vol > max_vol_8ch:
                raise RunConfigError(
                    f"Target volume for {dupe_name!r} must be <= "
                    f"{max_vol_8ch:.1f} μL (tip capacity for pipette "
                    f"{self.PIPETTE_ALIAS_8CH!r})")

        prefill_vol = target_vol_total - max_vol_1ch

        if (prefill_vol > max_vol_8ch) or (prefill_vol > max_vol_stock):
            raise ValueError(
                f"Combined target volumes for {plates_desc} are too great for "
                "the specified configuration of tips and labware")

        return max(0., prefill_vol)

    def _clip_aliquot_vol(
            self, plate_name: str, well_name: str, aliquot_vol: float,
            source_vol: float, split_factor: Optional[float] = None,
            target_count: Optional[float] = None):
        if split_factor is not None:
            target_desc = \
                f"split factor (1/{split_factor:.1f} -> {aliquot_vol:.1f} μL)"
        elif target_count is not None:
            target_desc = (
                f"seeding target ({target_count*1e-3:.1f}k cells -> "
                f"{aliquot_vol:.1f} μL)")

        if (target_count is not None) and not (source_vol > 0.):
            self.log_warning(
                f"Plate {plate_name!r} well {well_name}: no cells left - "
                "will skip transfer")
            return None

        max_vol = self.get_tip_capacity(self.pipette_1ch)
        min_vol = self.get_splitting_param('min_cell_aliquot_vol')
        if (aliquot_vol >= max_vol) and (source_vol > max_vol):
            aliquot_vol = max_vol
            self.log_warning(
                f"Plate {plate_name!r} well {well_name}: {target_desc} out of "
                f"pipetting range - aliquoting max volume ({aliquot_vol:.1f} "
                "μL)")
        elif aliquot_vol >= source_vol:
            aliquot_vol = source_vol
            self.log_warning(
                f"Plate {plate_name!r} well {well_name}: not enough cells to "
                f"meet {target_desc} - aliquoting available volume "
                f"({aliquot_vol:.1f} μL)")
        elif aliquot_vol <= min_vol:
            aliquot_vol = min_vol
            self.log_warning(
                f"Plate {plate_name!r} well {well_name}: {target_desc} out of "
                f"pipetting range - aliquoting min volume ({aliquot_vol:.1f} "
                "μL)")

        return aliquot_vol

    def calc_aliquot_by_split(
            self, plate_name: str, well_name: str,
            dupe_plates: Iterable[str], split_factor: Optional[float] = None,
            no_clip: bool = False) -> Optional[float]:
        if split_factor is None:
            split_factors = self.get_splitting_param(
                'target_split_factors', plate_name)
            if well_name not in split_factors:
                raise RunConfigError("No split factor found for "
                                     f"plate {plate_name!r} well {well_name}")
            split_factor = split_factors[well_name]
        if not split_factor or split_factor < 0.:
            self.log_info(f"Plate {plate_name!r} well {well_name}: "
                          f"split factor is zero - will skip transfer")
            return None
        source_vol = self.get_pretransfer_vol(well_name, cci=False)
        dupes_fill_vol = sum(
            self.get_splitting_param('dest_target_vols', dupe_name)
            for dupe_name in dupe_plates)
        stock_fill_vol = \
            self.get_splitting_param('dest_target_vols', plate_name)
        total_fill_vol = dupes_fill_vol + stock_fill_vol
        multiplier = total_fill_vol / stock_fill_vol

        aliquot_vol = multiplier * source_vol / split_factor

        if no_clip:
            return aliquot_vol
        return self._clip_aliquot_vol(
            plate_name, well_name, aliquot_vol, source_vol,
            split_factor=split_factor)

    def calc_aliquots_by_split(
            self, plate_name: str, col_no: int, split_in_place: bool = False,
            dupe_plates: Iterable[str] = ()) -> Dict[str, Optional[float]]:
        if split_in_place:
            plate = self.source_plate
        else:
            plate = self.dest_plates[plate_name]

        well_names = [well.well_name
                      for col in plate.columns(str(col_no))
                      for well in col]
        aliquot_vols = OrderedDict()
        for well_name in well_names:
            aliquot_vols[well_name] = self.calc_aliquot_by_split(
                plate_name, well_name, dupe_plates=dupe_plates,
                no_clip=split_in_place)
        return aliquot_vols

    def calc_aliquot_by_density(
            self, density: float, plate_name: str,
            well_name: str, dupe_plates: Iterable[str] = (),
            target_count: Optional[float] = None,
            target_vol: Optional[float] = None,
            source_vol: Optional[float] = None,
            presusp: bool = False
            ) -> Optional[float]:
        if target_count is None:
            target_counts = self.get_splitting_param(
                'target_counts', plate_name)
            if well_name not in target_counts:
                self.log_info(f"Plate {plate_name!r} well "
                              f"{well_name}: No target count found")
                return None
            target_count = target_counts[well_name]
        if not target_count or target_count < 0.:
            self.log_info(f"Plate {plate_name!r} well {well_name}: "
                          f"target is zero, will skip transfer")
            return None
        if source_vol is None:
            source_vol = self.get_pretransfer_vol(well_name, cci=True,
                                                  presusp=presusp)

        dupes_fill_vol = sum(
            self.get_splitting_param('dest_target_vols', dupe_name)
            for dupe_name in dupe_plates)

        if target_vol is not None:
            stock_fill_vol = target_vol
        else:
            stock_fill_vol = self.get_splitting_param('dest_target_vols', plate_name)

        total_fill_vol = dupes_fill_vol + stock_fill_vol
        multiplier = total_fill_vol / stock_fill_vol

        if not (density > 0.):
            aliquot_vol = self.get_tip_capacity(self.pipette_1ch)
        else:
            aliquot_vol = multiplier * target_count / density

        aliquot_vol = self._clip_aliquot_vol(
            plate_name, well_name, aliquot_vol, source_vol,
            target_count=target_count)

        mult_desc = f"(x {multiplier:.1f}) " if multiplier != 1. else ""
        self.log_info(f"Plate {plate_name!r} well {well_name}: "
                      f"target = {target_count/1e3:.1f}k {mult_desc}cells, "
                      f"aliquot = {aliquot_vol:.1f} μL")
        return aliquot_vol

    def calc_aliquots_by_density(
            self, densities: Dict[str, float], plate_name: str,
            dupe_plates: Iterable[str] = (), presusp: bool = False,
            target_vol: Optional[float] = None
            ) -> Dict[str, Optional[float]]:
        plate = self.dest_plates[plate_name]
        well_names = self.get_source_well_names(include=densities.keys())
        aliquot_vols = OrderedDict()
        for well_name in well_names:
            aliquot_vols[well_name] = \
                self.calc_aliquot_by_density(
                    densities[well_name], plate_name, well_name,
                    dupe_plates=dupe_plates, presusp=presusp,
                    target_vol=target_vol)
        return aliquot_vols

    def calc_aliquots_by_density_multidest(
            self, densities: Dict[str, float], plate_names: Iterable[str],
            presusp: bool = False,
            source_vols: Optional[Dict[str, float]] = None
            ) -> Dict[str, Dict[str, Optional[float]]]:
        well_names = self.get_source_well_names(include=densities.keys())
        if source_vols is None:
            source_vols = {
                well_name: self.get_pretransfer_vol(well_name, presusp=presusp)
                for well_name in well_names
                }
        else:
            source_vols = dict(source_vols)

        aliquot_vols: Dict[str, Dict[str, Optional[float]]] = OrderedDict()
        for plate_name in plate_names:
            plate_aliquots = OrderedDict()
            plate = self.dest_plates[plate_name]
            for well_name in well_names:
                if well_name not in {
                        w.well_name for w in plate.wells_by_name().keys()}:
                    continue
                well_aliquot = self.calc_aliquot_by_density(
                    densities[well_name], plate_name, well_name,
                    presusp=presusp, source_vol=source_vols[well_name])
                if well_aliquot is not None:
                    source_vols[well_name] = max(
                        0., source_vols[well_name] - well_aliquot)
                plate_aliquots[well_name] = well_aliquot
            aliquot_vols[plate_name] = plate_aliquots
        return aliquot_vols

    def get_next_trough_well(self, liquid: Union['CellSplitting.Liquid', str],
                             pipette: Optional[PipetteRef] = None):
        if pipette is not None:
            pipette_alias, pipette = self.match_pipette_ref(pipette)
        is_single = (pipette is not None) and (pipette.channels == 1)

        liquid = self.Liquid(liquid)
        if liquid.value in self.trough_fixed_cycles:
            well = next(self.trough_fixed_cycles[liquid.value])
        else:
            well = sorted(
                self.trough_wells[liquid.value],
                key=lambda x: (self.trough_withdrawn_vol[x.well_name],
                               x.well_name)
                )[0]

        # Offset to avoid smashing tips on the bottom ridges with the 1ch
        # (default OT behavior centers the tip over the well)
        return (
            well if not is_single
            else well.bottom(1.).move(opentrons.types.Point(y=4.5)))

    def do_flat_mix(
        self,
        pipette: PipetteRef,
        slot_no: int,
        well_name: str,
        stage: Optional[Union['CellSplitting.FlatMixStage', str]] = None,
        n_cycles: Optional[int] = None,
        well_vol: Optional[float] = None
            ):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        plate = self.labware[slot_no]
        well = plate.wells_by_name()[well_name]
        if stage is not None:
            stage = self.FlatMixStage(stage)
            if n_cycles is None:
                n_cycles = self.get_splitting_param(
                    f'flat_mix_n_cycles_{stage.value}')
            if well_vol is None:
                plate_name = self._get_dest_plate_name(slot_no)
                if stage == self.FlatMixStage.POSTTRANSFER:
                    well_vol = self.get_splitting_param(
                        'dest_target_vols', plate_name)
                else:
                    raise ValueError("must explicitly specify well_vol for "
                                     f"stage={stage.value!r}")
        if n_cycles is None or well_vol is None:
            raise ValueError("must specify stage or (n_cycles and well_vol) "
                             "for do_flat_mix()")
        flow_rate = self.get_splitting_param('mix_flow_rate')

        max_vol = self.get_tip_capacity(pipette)
        mix_vol = min(max_vol - 1., 0.5 * well_vol)
        BOTTOM_CLEARANCE = 1.
        mix_radius = well.diameter / 2. - 1.

        first = True
        for _ in range(n_cycles):
            for x, y in ((0., 0.), (1., 0.), (0., 1.),
                         (-1., 0.), (0., -1.), (0., 0.)):
                self.aspirate(
                    pipette,
                    mix_vol + (1. if first else 0.),
                    well.bottom(z=BOTTOM_CLEARANCE).move(
                        opentrons.types.Point(
                            x=x * mix_radius, y=y * mix_radius)),
                    flow_rate=flow_rate
                )
                self.dispense(
                    pipette,
                    mix_vol,
                    well.bottom(z=BOTTOM_CLEARANCE).move(
                        opentrons.types.Point(
                            x=x * mix_radius, y=y * mix_radius)),
                    flow_rate=flow_rate
                )
                if first:
                    first = False
        self.dispense(pipette, flow_rate=flow_rate)

    def do_flat_mix_1ch(self, *args, **kwargs):
        self.do_flat_mix(pipette=self.pipette_1ch, *args, **kwargs)

    def do_flat_mix_8ch(self, slot_no: int, col_no: int, **kwargs):
        self.do_flat_mix(pipette=self.pipette_8ch, slot_no=slot_no,
                         well_name=f"A{col_no}", **kwargs)

    def do_tilted_mix(
        self,
        pipette: PipetteRef,
        well_name: str,
        stage: Optional[Union['CellSplitting.TiltedMixStage', str]] = None,
        n_cycles: Optional[int] = None,
        well_vol: Optional[float] = None,
        mix_portion: Optional[float] = None,
        cci: bool = True,
        presusp: bool = False,
        plunger_boost: Optional[bool] = None,
        motor_current: Optional[float] = None,
            ):
        pipette_alias, pipette = self.match_pipette_ref(pipette)
        if stage is not None:
            stage = self.TiltedMixStage(stage)
            stage_desc = f" for stage {stage.value!r}"
            if n_cycles is None:
                n_cycles = self.get_splitting_param(
                    f'tilted_mix_n_cycles_{stage.value}')
            if plunger_boost is None:
                plunger_boost = self.get_splitting_param(
                    f'tilted_mix_plunger_boost_{stage.value}', default=None)
            if motor_current is None:
                 motor_current = self.get_splitting_param(
                    f'tilted_mix_motor_current_{stage.value}', default=None)
            if mix_portion is None:
                mix_portion = self.get_splitting_param(
                    f'tilted_mix_portion_{stage.value}', default=None)
            if well_vol is None:
                if stage == self.TiltedMixStage.FROMSUSP:
                    well_vol = self.get_presusp_vol(well_name)
                elif stage == self.TiltedMixStage.POSTQUENCH:
                    well_vol = self.get_postquench_vol(well_name)
                elif stage == self.TiltedMixStage.PRETRANSFER:
                    well_vol = self.get_pretransfer_vol(
                        well_name, cci=cci, presusp=presusp)
        else:
            stage_desc = ""
        if n_cycles is None or well_vol is None:
            raise ValueError("must specify stage or (n_cycles and well_vol) "
                             "for do_tilted_mix()")
        if mix_portion is None:
            mix_portion = 0.6

        boost_desc = (
            (f", plunger boost ON" if plunger_boost else "")
            + (f", curr={motor_current:.2f}A"
               if motor_current is not None else "")
            )
        self.comment(f"Mixing at source plate location {well_name!r}, "
                     f"{n_cycles} cycles{stage_desc}, "
                     f"well vol {well_vol:.1f} μL{boost_desc}", also_log=True)

        well_corner = self.get_source_well_bottom_corner_loc(well_name)
        well_center = self.get_source_well_bottom_center_loc(well_name)
        approach = self.get_source_well_approach_loc(well_name)

        max_vol = self.get_tip_capacity(pipette) - 1.
        flow_rate = self.get_splitting_param('mix_flow_rate')
        mix_vol_corner = min(well_vol * mix_portion, max_vol)
        mix_vol_center = max(0., min(mix_vol_corner - 40., well_vol - 70.))
        if mix_vol_center == 0.:
            n_cycles *= 2

        pipette.move_to(approach)
        with PlungerBoost(self, pipette, fullstep=plunger_boost,
                          motor_current=motor_current):
            self.aspirate(pipette, mix_vol_corner + 1., well_corner,
                          flow_rate=flow_rate)
            for i in range(n_cycles - 1):
                self.dispense(pipette, mix_vol_corner, flow_rate=flow_rate)
                if mix_vol_center > 0.:
                    self.aspirate(pipette, mix_vol_center, well_center,
                                  flow_rate=flow_rate)
                    self.dispense(pipette, mix_vol_center, flow_rate=flow_rate)
                self.aspirate(pipette, mix_vol_corner, well_corner,
                              flow_rate=flow_rate)
        self.dispense(pipette, flow_rate=flow_rate)

    def do_tilted_mix_1ch(self, *args, **kwargs):
        self.do_tilted_mix(pipette=self.pipette_1ch, *args, **kwargs)

    def do_tilted_mix_8ch(self, col_no: int, **kwargs):
        self.do_tilted_mix(
            pipette=self.pipette_8ch, well_name=f"A{col_no}", **kwargs)

    def aspirate_old_media(self, set_idx: int):
        pipette = self.pipette_8ch
        col_nos = self.get_col_nos(set_idx)
        set_desc = f"set {set_idx} (plate cols {','.join(map(str, col_nos))})"
        max_vol = self.get_tip_capacity(pipette)

        self.comment(f"Starting to aspirate old media from {set_desc}...",
                     also_log=True)

        for col_no in col_nos:
            well_corner = self.get_source_well_bottom_corner_loc(f'A{col_no}')
            approach = self.get_source_well_approach_loc(f'A{col_no}')

            pipette.pick_up_tips()
            pipette.move_to(approach)
            self.aspirate(pipette, max_vol, well_corner, flow_rate=40.)
            self.dispense(pipette, max_vol,
                          self.get_next_trough_well(self.Liquid.WASTE),
                          flow_rate=999.)
            pipette.drop_tips()

        self.comment(f"Aspirating old media from set {set_idx} complete.",
                     also_log=True)

    def do_cell_wash(self, set_idx: int,
                     liquid: Optional[Union['CellSplitting.Liquid',
                                            str]] = None,
                     wash_vol: Optional[float] = None,
                     dispense_rate: Optional[float] = None,
                     aspirate_rate: Optional[float] = None):
        col_nos = self.get_col_nos(set_idx)
        if liquid is None:
            liquid = self.Liquid.PBS
        else:
            liquid = self.Liquid(liquid)
        if wash_vol is None:
            wash_vol = self.get_splitting_param('pbs_wash_vol')
        if dispense_rate is None:
            dispense_rate = self.get_splitting_param('pbs_wash_dispense_rate')
        if aspirate_rate is None:
            aspirate_rate = self.get_splitting_param('pbs_wash_aspirate_rate')
        pipette = self.pipette_8ch
        max_vol = self.get_tip_capacity(pipette)

        set_desc = f"set {set_idx} (plate cols {','.join(map(str, col_nos))})"
        self.comment(f"Starting to wash {set_desc}...", also_log=True)

        for col_no in col_nos:
            well_corner = self.get_source_well_bottom_corner_loc(f'A{col_no}')
            approach = self.get_source_well_approach_loc(f'A{col_no}')
            wall = self.get_source_well_wall_loc(f'A{col_no}')

            pipette.pick_up_tips()
            pipette.aspirate(wash_vol,
                             self.get_next_trough_well(self.Liquid.PBS))
            pipette.move_to(approach)
            self.dispense(pipette, wash_vol, wall, flow_rate=dispense_rate)
            pipette.move_to(approach)
            self.aspirate(pipette, max_vol, well_corner,
                          flow_rate=aspirate_rate)
            self.dispense(pipette, max_vol,
                          self.get_next_trough_well(self.Liquid.WASTE),
                          flow_rate=999.)
            pipette.drop_tips()

        self.comment(f"Washing set {set_idx} complete.", also_log=True)

    def trypsinize(self, set_idx: int, reuse_tips: Optional[bool] = None):
        # TODO see note on quench()
        if reuse_tips is None:
            reuse_tips = self.get_splitting_param('trypsinize_reuse_tips')
        col_nos = self.get_col_nos(set_idx)
        set_desc = f"set {set_idx} (plate cols {','.join(map(str, col_nos))})"
        pipette = self.pipette_8ch

        tip_mode_desc = "" if reuse_tips else " (not reusing tips)"
        self.comment(
            f"Starting to add trypsin to {set_desc}{tip_mode_desc}...",
            also_log=True)

        pipette.pick_up_tips()

        trypsin_well = self.get_next_trough_well(self.Liquid.TRYPSIN)
        for col_no in col_nos:
            col_start_well = f'A{col_no}'
            trypsin_vol = self.get_trypsin_vol(col_start_well)
            pipette.aspirate(trypsin_vol, trypsin_well)
            self.dispense(pipette, trypsin_vol,
                          self.get_source_well_top_loc(col_start_well),
                          flow_rate=300.)
            self.log_debug(
                f"Dispensed {trypsin_vol:.1f} μL trypsin for column {col_no}")
            if col_no == col_nos[-1]:
                self.col_sets[set_idx].trypsin_start_time = time.monotonic()
            pipette.aspirate(20., in_air=True)
            trypsin_well = self.get_next_trough_well(self.Liquid.TRYPSIN)
            pipette.dispense(location=trypsin_well.top())
            if col_no != col_nos[-1] and not reuse_tips:
                pipette.drop_tips()
                pipette.pick_up_tips()

        pipette.drop_tips()
        self.comment(
            f"Adding trypsin to set {set_idx} complete.", also_log=True)

    def quench(self, set_idx: int, reuse_tips: Optional[bool] = None,
               mix_after: bool = False,
               mix_stage: Optional['CellSplitting.TiltedMixStage'] = None):
        # TODO: This is almost identical to the trypsinize fn --
        # maybe try to deduplicate some code
        cherryskipping_mode = \
                self.get_splitting_param('cherryskipping_mode', default=None)
        if reuse_tips is None and cherryskipping_mode:
            reuse_tips = False
        if reuse_tips is None:
            reuse_tips = self.get_splitting_param('quench_reuse_tips')
        if mix_stage is not None:
            mix_stage = self.TiltedMixStage(mix_stage)
        else:
            mix_stage = self.TiltedMixStage.POSTQUENCH
        col_nos = self.get_col_nos(set_idx)
        set_desc = f"set {set_idx} (plate cols {','.join(map(str, col_nos))})"
        pipette = self.pipette_8ch

        if not self.col_set_quench_ready(set_idx):
            self.log_info(f"Waiting to quench {set_desc}...")
            while not self.col_set_quench_ready(set_idx):
                # Calculate the remaining time and do a
                # delay() instead of this while-loop?
                time.sleep(1.)

        tip_mode_desc = "" if reuse_tips else " (not reusing tips)"
        self.comment(f"Starting to quench {set_desc}{tip_mode_desc}...",
                     also_log=True)

        pipette.pick_up_tips()

        media_well = self.get_next_trough_well(self.Liquid.MEDIA)
        for col_no in col_nos:
            col_start_well = f'A{col_no}'
            quench_vol = self.get_quench_vol(col_start_well)
            pipette.aspirate(quench_vol, media_well)
            self.dispense(pipette, quench_vol,
                          self.get_source_well_top_loc(col_start_well),
                          flow_rate=300.)
            self.log_debug(
                f"Dispensed {quench_vol:.1f} μL media "
                f"to quench column {col_no}")

            media_well = self.get_next_trough_well(self.Liquid.MEDIA)

            if mix_after:
                self.do_tilted_mix_8ch(col_no, stage=mix_stage)
            else:
                pipette.aspirate(20., in_air=True)
                pipette.dispense(location=media_well.top())

            if col_no != col_nos[-1] and not reuse_tips:
                pipette.drop_tips()
                pipette.pick_up_tips()

            self.sim_add_liquid(
                self.get_slot_no(self.source_plate),
                [x.well_name for x in
                    self.source_plate.columns_by_name()[str(col_no)]],
                self.Liquid.CELLS.value,
                1e-3
                )

        pipette.drop_tips()
        self.comment(f"Quenching set {set_idx} complete.", also_log=True)
        self.col_sets[set_idx].quenched = True

    def do_prequench_steps(self, set_idx: int,
                           reuse_tips: Optional[bool] = None):
        if (reuse_tips is None and
                self.get_splitting_param('cherryskipping_mode', default=None)):
            reuse_tips = False

        self.aspirate_old_media(set_idx)
        self.do_cell_wash(set_idx)
        self.trypsinize(set_idx, reuse_tips=reuse_tips)

    def aliquot_from_source_plate(
        self,
        dest_name: str,
        well_name: str,
        vol: Optional[float],
        dest_well: Optional[str] = None,
        target_vol: Optional[float] = None,
        topup_liquid: Optional[Union['CellSplitting.Liquid', str]] = None,
        dupe_plates: Iterable[str] = (),
        skip_zero: bool = True,
        cci: bool = True,
        presusp: bool = False,
        pipette: Optional[PipetteRef] = None,
        mix_stage: Optional[Union['CellSplitting.TiltedMixStage', str]] = None,
            ):
        if pipette is not None:
            _, pipette = self.match_pipette_ref(pipette)
        else:
            pipette = self.pipette_1ch
        if dest_well is None:
            dest_well = well_name
        if mix_stage is not None:
            mix_stage = self.TiltedMixStage(mix_stage)
        else:
            mix_stage = self.TiltedMixStage.PRETRANSFER

        self.comment(f"Aliquoting cells from source well {well_name} "
                     f"to well {dest_well} on plate '{dest_name}'.")
        if skip_zero and not vol:
            self.log_info(f"Not aliquoting from source well {well_name} "
                          f"to well {dest_well} on plate '{dest_name}'.")
            return

        if topup_liquid is not None:
            topup_liquid = self.Liquid(topup_liquid)
        else:
            topup_liquid = self.Liquid.MEDIA
        if target_vol is None:
            target_vol = (
                sum(self.get_splitting_param('dest_target_vols', dupe_name)
                    for dupe_name in tuple(dupe_plates) + (dest_name,))
                - self.get_dupe_prefill_vol(dest_name, dupe_plates))

        max_vol = self.get_tip_capacity(pipette)

        vol = min(max_vol, max(0., vol))
        media_vol = min(max_vol, max(0., target_vol - vol))

        media_desc = (f"{media_vol:.1f} μL of {topup_liquid.value!r} and "
                      if media_vol > 0. else "")
        self.log_info(f"Aliquoting {media_desc}{vol:.1f} μL "
                      f"from source well {well_name} "
                      f"to well {dest_well} on plate '{dest_name}'.")

        destination_well =self.dest_plates[dest_name].wells_by_name()[dest_well]

        pipette.pick_up_tip()

        if media_vol > 0.:
            pipette.aspirate(
                media_vol, self.get_next_trough_well(
                    topup_liquid, pipette=pipette))
            pipette.dispense(media_vol, destination_well)

        self.do_tilted_mix(pipette=pipette, well_name=well_name,
                           stage=mix_stage,
                           cci=cci, presusp=presusp)

        pipette.aspirate(vol, self.get_source_well_bottom_corner_loc(well_name))
        pipette.dispense(vol, destination_well)

        pipette.aspirate(20., destination_well.top(), in_air=True)

        pipette.drop_tip()

    def discard_and_backfill_source_well(
            self,
            well_name: str,
            vol: Optional[float],  # volume of cell suspension to leave
            target_vol: Optional[float] = None,  # target vol to backfill to
            topup_liquid: Optional[Union['CellSplitting.Liquid', str]] = None,
            skip_zero: bool = True
            ):
        self.comment(f"Splitting source plate well {well_name} in place")

        if vol is None or skip_zero and not vol:
            self.log_info(f"Not splitting source plate well {well_name}.")
            return

        if topup_liquid is not None:
            topup_liquid = self.Liquid(topup_liquid)
        else:
            topup_liquid = self.Liquid.MEDIA

        if target_vol is None:
            target_vol = self.get_splitting_param(
                'dest_target_vols', 'source_plate')

        pipette = self.pipette_1ch
        waste_trough = self.get_next_trough_well(self.Liquid.WASTE)

        max_vol = self.get_tip_capacity(pipette)

        initial_well_volume = self.get_postquench_vol(well_name)
        discard_vol = min(max_vol, max(0., initial_well_volume - vol))
        media_vol = min(max_vol, max(
            0., target_vol - initial_well_volume + discard_vol))
        # TODO: Generate warnings if clipping occurs

        media_desc = (
            f" and replacing {media_vol:.1f} μL of {topup_liquid.value!r}"
            if media_vol > 0. else "")
        self.log_info(f"Discarding {discard_vol:.1f} μL "
                      f"from source well {well_name}{media_desc}.")

        # mix and discard
        if discard_vol > 0.:
            pipette.pick_up_tip()
            self.do_tilted_mix_1ch(well_name=well_name,
                                   stage=self.TiltedMixStage.PRETRANSFER,
                                   cci=False)
            self.aspirate(pipette, discard_vol,
                          self.get_source_well_bottom_corner_loc(well_name))
            self.dispense(pipette, max_vol, waste_trough, flow_rate=999.0)
            pipette.drop_tip()

        if media_vol > 0.:
            pipette.pick_up_tip()
            pipette.aspirate(
                media_vol, self.get_next_trough_well(
                    topup_liquid, pipette=pipette))
            pipette.dispense(
                media_vol, self.get_source_well_bottom_corner_loc(well_name))
            pipette.drop_tip()

    def transfer_to_dupe(self, from_name: str, dupe_name: str,
                         vol: Optional[float] = None,
                         col_nos: Optional[Iterable[int]] = None):
        if vol is None:
            vol = self.get_splitting_param('dest_target_vols', dupe_name)
        col_nos, cols_desc = self._get_dest_plate_col_info(dupe_name, col_nos)
        pipette = self.pipette_8ch
        from_slot = self.get_slot_no(self.dest_plates[from_name])
        dupe_slot = self.get_slot_no(self.dest_plates[dupe_name])
        starting_vol = \
            vol + self.get_splitting_param('dest_target_vols', from_name)

        self.comment(f"Transferring {vol:.1f} μL from {cols_desc} on "
                     f"{from_name!r} (slot {from_slot}) to {dupe_name!r} "
                     f"(slot {dupe_slot})", also_log=True)
        for col_no in col_nos:
            pipette.pick_up_tips()
            self.do_flat_mix_8ch(slot_no=from_slot, col_no=col_no,
                                 stage=self.FlatMixStage.PRETRANSFER,
                                 well_vol=starting_vol)
            pipette.aspirate(
                vol, self.dest_plates[from_name].wells_by_name()[f'A{col_no}'])
            pipette.dispense(
                vol, self.dest_plates[dupe_name].wells_by_name()[f'A{col_no}'])
            self.do_flat_mix_8ch(slot_no=dupe_slot, col_no=col_no,
                                 stage=self.FlatMixStage.POSTTRANSFER)
            pipette.drop_tips()
        self.comment(f"Transferring from {from_name!r} "
                     f"to {dupe_name!r} done.", also_log=True)

    def prefill_dest_plate(
            self, dest_name: str, vol: float,
            liquid: Optional['CellSplitting.Liquid'] = None,
            col_nos: Optional[Iterable[int]] = None,
            handle_tips: bool = True):
        pipette = self.pipette_8ch
        col_nos, cols_desc = self._get_dest_plate_col_info(dest_name, col_nos)
        if liquid is not None:
            liquid = self.Liquid(liquid)
        else:
            liquid = self.Liquid.MEDIA
        if not vol:
            self.comment(f"Not prefilling {cols_desc} of plate {dest_name!r} "
                         f"with {liquid.value!r}", also_log=True)
            return
        dest_plate = self.dest_plates[dest_name]

        self.comment(f"Prefilling {cols_desc} of plate {dest_name!r} with "
                     f"{vol:.1f} μL of {liquid.value!r}...", also_log=True)
        if handle_tips:
            pipette.pick_up_tips()
        trough_well = self.get_next_trough_well(liquid)
        for col_no in col_nos:
            pipette.aspirate(vol, trough_well)
            well = dest_plate.wells_by_name()[f'A{col_no}']
            pipette.dispense(vol, well.bottom(1.))
            pipette.aspirate(20., well.top(), in_air=True)
            trough_well = self.get_next_trough_well(liquid)
            pipette.dispense(location=trough_well.top())
        if handle_tips:
            pipette.drop_tips()
        self.comment(
            f"Prefilling plate {dest_name!r} done.", also_log=True)

    def deactivate_tempdeck(self):
        self.log_info("Deactivating tempdeck.")
        self.tempdeck.deactivate()

    def set_tempdeck(self, temp: Optional[float] = None,
                     stage: Optional[str] = None):
        if stage is not None:
            stage = self.TempdeckStage(stage)
        if stage == self.TempdeckStage.COAST:
            self.deactivate_tempdeck()
            return
        if temp is None:
            if stage is None:
                raise ValueError(
                    "must specify either temp or stage for set_tempdeck()")
            temp = self.get_splitting_param(f'tempdeck_temp_{stage.value}')
        self.log_info(f"Setting tempdeck to {temp:.1f} °C...")
        self.tempdeck.set_temperature(temp)
        self.log_info("Tempdeck reached setpoint.")


class CciCellSplitting(CellSplitting):
    CCI_INSERTION_SPEED = 5.
    CCI_N_PULLOUT_STEPS = 10
    CCI_PULLOUT_STEP_VOL = 1.
    CCI_PULLOUT_SPEED = 1.
    CCI_PRE_LOADING_DELAY = 2.
    CCI_LOADING_FLOW_RATE = 60.
    SAMPLING_FLOW_RATE = 50.

    cci_client: Optional['CciClient'] = None

    def init_labware(self):
        CellSplitting.init_labware(self)

        self.cci_flow_cell = self.load_labware(
            slot_no=self.get_splitting_param('cci_slot'),
            def_name=self.get_splitting_param('cci_def'),
            name='cci'
            )
        self.cci_approach_loc = \
            self.cci_flow_cell.wells_by_name()['A1'].top(10.)
        self.cci_loading_loc = \
            self.cci_flow_cell.wells_by_name()['A1'].top(-5.)

    def init_cci(self):
        sim_use_cci = globals().get("SIM_USE_CCI", False)
        cci_url = self.get_cci_url()
        self.comment(
            f"Attempting to connect to CCI at {cci_url!r}...", also_log=True)
        cci_cls = (
            DummyCciClient if self.is_simulating() and not sim_use_cci
            else CciClient)
        cci_client = cci_cls(cci_url)

        while True:
            try:
                cci_client.start_new_record()
                break
            except CciError as e:
                err_msg = f"Error while attempting contact with CCI: {e}"
                self.log_error(err_msg)
                self.pause_with_message(
                    "Problem contacting CCI server on startup")
                if self.is_simulating():
                    time.sleep(2.)

        self.cci_client = cci_client
        self.comment("Connected to CCI, started new counting record.",
                     also_log=True)

    def get_cci_url(self):
        url = globals().get(
            "SIM_OVERRIDE_CCI_URL",
            self.get_run_param('cci', 'force_url'))
        if url is not None:
            self.log_debug(f"CCI URL override: '{url!r}'")
            return url
        robot_id = self.get_robot_id()
        try:
            url = self.get_run_param('cci', 'server_urls')[robot_id]
        except (KeyError, RunConfigError) as e:
            raise RunConfigError(
                "Couldn't determine CCI server URL to use for this run")
        self.log_debug(f"CCI URL for robot {robot_id!r}: {url!r}")
        return url

    def _assert_cci_initialized(self):
        caller_name = inspect.currentframe().f_back.f_code.co_name
        if self.cci_client is None:
            raise RuntimeError(
                f"init_cci() must be called before {caller_name}()")

    def do_cci_bg_scan(self):
        self._assert_cci_initialized()
        self.comment(f"Running CCI background scan...", also_log=True)
        while True:
            try:
                self.cci_client.do_bg_scan()
                break
            except CciError as e:
                err_msg = f"Error while attempting CCI background scan: {e}"
                self.log_error(err_msg)
                self.pause_with_message(f"CCI background scan error: {e}")
                if self.is_simulating():
                    time.sleep(2.)
        self.comment(f"CCI background scan complete.", also_log=True)

    def do_cci_counting_scan(self, col_no: int) -> Dict[str, float]:
        self._assert_cci_initialized()
        density_clip_min = self.get_splitting_param('cci_density_clip_min')
        density_clip_max = self.get_splitting_param('cci_density_clip_max')
        valid_area_warn_thresh = \
            self.get_splitting_param('cci_valid_area_warn_thresh')
        well_names = tuple(
            x.well_name
            for x in self.source_plate.columns_by_name()[str(col_no)])
        assert len(well_names) == 8

        self.comment(
            f"Running CCI counting scan for source plate column {col_no}...",
            also_log=True)
        while True:
            try:
                densities, valid_areas, warnings = \
                    self.cci_client.do_counting_scan()
                break
            except CciError as e:
                err_msg = f"Error while attempting CCI counting scan: {e}"
                self.log_error(err_msg)
                self.pause_with_message("CCI error on counting scan")
                if self.is_simulating():
                    time.sleep(2.)
        self.comment("CCI counting scan complete.", also_log=True)

        densities_by_well = OrderedDict(zip(well_names, densities))
        valid_areas_by_well = dict(zip(well_names, valid_areas))
        warnings_by_well = dict(zip(well_names, warnings))

        for well_name, density in densities_by_well.items():
            for warning_desc in warnings_by_well.get(well_name, ()):
                self.log_warning(f"Well {well_name}: "
                                 f"CCI warning: {warning_desc}")
            if valid_areas_by_well[well_name] < valid_area_warn_thresh:
                self.log_warning(
                    f"Well {well_name}: Low valid_area "
                    f"({valid_areas_by_well[well_name]*100.:.0f}%) for image "
                    "- possible bubble or obstruction")
            if density > density_clip_max:
                densities_by_well[well_name] = density_clip_max
                self.log_warning(
                    f"Well {well_name}: High measurement value {density:.1f} "
                    f"cells/μL clipped to {densities_by_well[well_name]:.1f} "
                    "cells/μL")
            elif density < density_clip_min:
                densities_by_well[well_name] = density_clip_min
                self.log_warning(
                    f"Well {well_name}: Low measurement value {density:.1f} "
                    f"cells/μL clipped to {densities_by_well[well_name]:.1f} "
                    "cells/μL")
            self.log_info(f"Well {well_name}: density = {density:.1f} "
                          "cells/μL, valid_area = "
                          f"{valid_areas_by_well[well_name]*100.:.0f}%")

        return densities_by_well

    def load_cci_from_source_plate(self, col_no: int,
                                   handle_tips: bool = False):
        top_well = f'A{col_no}'
        pipette = self.pipette_8ch
        corner_loc = self.get_source_well_bottom_corner_loc(top_well)
        loading_vol = self.get_splitting_param('cci_loading_vol')

        self.comment(
            f"Loading CCI from source column {col_no}.", also_log=True)
        if handle_tips:
            pipette.pick_up_tips()
        self.aspirate(pipette, loading_vol, corner_loc,
                      flow_rate=self.SAMPLING_FLOW_RATE)
        pipette.move_to(self.cci_approach_loc)
        pipette.move_to(self.cci_loading_loc, speed=self.CCI_INSERTION_SPEED)
        self.delay(self.CCI_PRE_LOADING_DELAY)
        self.dispense(pipette, loading_vol,
                      flow_rate=self.CCI_LOADING_FLOW_RATE)
        self.delay(self.get_splitting_param('cci_settling_time'))

    def pull_out_of_cci(self):
        self.pipette_8ch.move_to(
            self.cci_approach_loc, speed=self.CCI_INSERTION_SPEED)

    def prime_cci(
            self, liquid: Optional[Union['CellSplitting.Liquid', str]] = None):
        self.flush_cci(
            stage=self.FlushStage.PRIME, liquid=liquid, handle_tips=False)
        self.pipette_8ch.move_to(
            self.trash_locations[self.PIPETTE_ALIAS_8CH][0])

    def flush_cci(self, stage: Optional[Union['CellSplitting.FlushStage',
                                              str]] = None,
                  liquid: Optional[Union['CellSplitting.Liquid', str]] = None,
                  n_cycles: Optional[int] = None, handle_tips: bool = True,
                  reuse_tips: bool = None,
                  flush_rate: Optional[float] = None,
                  dwell_time: Optional[float] = None):
        action_desc = "Flushing"
        if stage is not None:
            stage = self.FlushStage(stage)
        if liquid is not None:
            liquid = self.Liquid(liquid)
        else:
            if stage is None:
                raise ValueError(
                    "must specify liquid or stage for flush_cci()")
            if stage == self.FlushStage.WASH:
                liquid = self.Liquid.DETERGENT
            elif stage == self.FlushStage.RINSE:
                liquid = self.Liquid.WATER
            else:
                liquid = self.Liquid.PBS
        if n_cycles is None:
            if stage == self.FlushStage.WASH:
                n_cycles = self.get_splitting_param('cci_wash_n_cycles')
            elif stage == self.FlushStage.RINSE:
                n_cycles = self.get_splitting_param('cci_rinse_n_cycles')
            else:
                n_cycles = 1
        if flush_rate is None and stage is not None:
            if stage == self.FlushStage.PRIME:
                flush_rate = self.get_splitting_param('cci_prime_rate')
                action_desc = "Priming"
            else:
                flush_rate = self.get_splitting_param('cci_flush_rate')
        if dwell_time is None and stage is not None:
            if stage == self.FlushStage.WASH:
                dwell_time = self.get_splitting_param('cci_wash_dwell_time')
        if reuse_tips is None:
            reuse_tips = (stage != self.FlushStage.PRIME)

        pipette = self.pipette_8ch
        flush_vol = self.get_splitting_param('cci_flush_vol')

        if not n_cycles:
            self.comment(f"Skipping CCI flush with {liquid.value!r}.",
                         also_log=True)
            return

        dwell_desc = (f" with {dwell_time:.1f} s dwell"
                      if dwell_time is not None
                      else "")
        self.comment(
            f"{action_desc} CCI with {n_cycles} x {flush_vol} μL "
            f"of {liquid.value!r}{dwell_desc}.",
            also_log=True)

        if handle_tips:
            pipette.pick_up_tips()

        for i in range(n_cycles):
            pipette.aspirate(flush_vol, self.get_next_trough_well(liquid))
            pipette.move_to(self.cci_approach_loc)
            pipette.move_to(self.cci_loading_loc,
                            speed=self.CCI_INSERTION_SPEED)
            self.delay(1.)

            initial_vol = (
                flush_vol
                - self.CCI_N_PULLOUT_STEPS * self.CCI_PULLOUT_STEP_VOL)
            self.dispense(pipette, vol=initial_vol, flow_rate=flush_rate)
            if dwell_time is not None:
                self.delay(dwell_time)
            for c in range(1, self.CCI_N_PULLOUT_STEPS + 1):
                z_offs = -5. * (1. - c / self.CCI_N_PULLOUT_STEPS)
                pipette.move_to(
                    self.cci_flow_cell.wells_by_name()['A1'].top(z_offs),
                    speed=self.CCI_PULLOUT_SPEED)
                self.dispense(pipette, self.CCI_PULLOUT_STEP_VOL)
            if i < n_cycles - 1 and handle_tips and not reuse_tips:
                pipette.drop_tips()
                pipette.pick_up_tips()

        if handle_tips:
            pipette.drop_tips()


class PlungerBoost:
    MAX_SPEED = 150.
    ACCEL = 1000.

    def __init__(self, ps: ProtocolSteps, pipette: PipetteRef,
                 fullstep: bool = True, max_speed: Optional[float] = None,
                 accel: Optional[float] = None,
                 motor_current: Optional[float] = None):
        if max_speed is None:
            max_speed = self.MAX_SPEED
        if accel is None:
            accel = self.ACCEL
        self.ps = ps
        self.pipette_alias, self.pipette = ps.match_pipette_ref(pipette)
        self.axes = self.get_plunger_axis(self.pipette)
        self.max_speed = max_speed
        self.accel = accel
        self.fullstep = fullstep
        self.motor_current = motor_current
        self._orig_instr_config = None

        if self.ps.is_simulating():
            self.smoothie = None
            self.hw_loop = None
            return
        hw = ps.ot_ctx._implementation.get_hardware().hardware
        mount = pipette._implementation._mount
        self.smoothie = hw._backend._smoothie_driver
        self.hw_loop = hw._loop
        self.instrument = hw._attached_instruments[mount]

    def __enter__(self):
        if self.fullstep:
            self.set_fullstepping(True)
        if self.motor_current is not None:
            self.set_motor_current(self.motor_current)
        self.set_max_speed(self.max_speed)
        self.set_accel(self.accel)

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.fullstep:
            self.set_fullstepping(False)
        if self.motor_current is not None:
            self.reset_motor_current()
        self.reset_max_speed()
        self.reset_accel()

    @staticmethod
    def get_plunger_axis(pipette) -> str:
        # TODO: maybe get this via Opentrons internal API rather than hardcoding
        return {'left': "B", 'right': "C"}[pipette.mount]

    def run_synchronously(self, fn: Callable, *args, **kwargs) -> Any:
        assert self.hw_loop is not None  # make TC happy

        if asyncio.iscoroutinefunction(fn):
            coro_fn = fn
        else:
            async def coro_fn(*args, **kwargs):
                return fn(*args, **kwargs)
        coro = coro_fn(*args, **kwargs)
        return asyncio.run_coroutine_threadsafe(coro, self.hw_loop).result()

    def set_fullstepping(self, on: bool):
        if self.smoothie is None:
            return
        on_cmd, off_cmd = \
            self.smoothie._build_fullstep_configurations(self.axes)
        cmd = on_cmd if on else off_cmd
        self.run_synchronously(self.smoothie._send_command, cmd)

    def set_motor_current(self, curr: float):
        if self.smoothie is None:
            return
        self._orig_instr_config = self.instrument._config
        config_dict = dict(self._orig_instr_config.__dict__)
        config_dict['plunger_current'] = float(curr)
        temp_config = type(self._orig_instr_config)(**config_dict)
        self.instrument._config = temp_config

    def reset_motor_current(self):
        if self.smoothie is None:
            return
        if self._orig_instr_config is not None:
            self.instrument._config = self._orig_instr_config
            self._orig_instr_config = None

    def set_max_speed(self, max_speed: float):
        max_speeds = {k: max_speed for k in self.axes}
        if self.smoothie is None:
            return
        self.run_synchronously(
            self.smoothie.set_axis_max_speed, max_speeds, update=True)

    def reset_max_speed(self):
        if self.smoothie is None:
            return
        defaults = self.smoothie._config.default_max_speed
        max_speeds = {k: v for (k, v) in defaults.items() if k in self.axes}
        self.run_synchronously(
            self.smoothie.set_axis_max_speed, max_speeds, update=True)

    def set_accel(self, accel: float):
        accels = {k: accel for k in self.axes}
        if self.smoothie is None:
            return
        self.run_synchronously(self.smoothie.set_acceleration, accels)

    def reset_accel(self):
        if self.smoothie is None:
            return
        defaults = self.smoothie._config.acceleration
        accels = {k: v for (k, v) in defaults.items() if k in self.axes}
        self.run_synchronously(self.smoothie.set_acceleration, accels)
