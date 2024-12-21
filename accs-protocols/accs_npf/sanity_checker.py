# TODO: Only include robot-moving steps in the "protocol steps" count?
# TODO: Housekeeping -- finish type hinting annotations, etc.
# TODO: Re-incorporate tip mapping

import collections
import itertools
import os
import string
import sys
from typing import Any, Callable, Iterable, List, Tuple

from .protocol_analyzer import (
    ProtocolAnalyzer, ProtocolScriptAborted, format_loc)


PLATE_SLOTS = ("1", "2", "3")  # TODO: generalize


def split_well_name(well_name: str) -> Tuple[str, int]:
    letter_part = well_name.rstrip(string.digits)
    number_part = well_name[len(letter_part):]
    return letter_part, int(number_part)


def group_consecutive(vals: Iterable, key_fn: Callable[[Any], int] = int):
    return [
        tuple(d for (c, d) in b)
        for (a, b) in itertools.groupby(
            enumerate(vals),
            lambda x: key_fn(x[1])-x[0])]


def collapse_well_names(well_names: Iterable[str]) -> List[str]:
    def get_idx(c):
        if len(c) > 1:
            raise ValueError("only single-letter row names supported")
        return string.ascii_uppercase.index(c.upper())

    parts = [split_well_name(x) for x in well_names]
    col_nos = sorted({col_no for (letter_part, col_no) in parts})
    cols_by_letter_group = collections.defaultdict(list)

    for col_no in col_nos:
        letter_parts = sorted([
            letter_part for (letter_part, col_no_) in parts
            if col_no_ == col_no])
        letter_groups = group_consecutive(letter_parts, key_fn=get_idx)
        for letter_group in letter_groups:
            cols_by_letter_group[letter_group].append(col_no)

    collapsed_names = []
    for (letter_group, group_cols) in sorted(
        cols_by_letter_group.items(),
        key=lambda x: x[1][0]
            ):
        for lg_col_group in group_consecutive(group_cols):
            if len(letter_group) > 1:
                letter_part = f"[{letter_group[0]}-{letter_group[-1]}]"
            else:
                letter_part = letter_group[0]
            if len(lg_col_group) > 1:
                number_part = f"[{lg_col_group[0]}-{lg_col_group[-1]}]"
            else:
                number_part = str(lg_col_group[0])
            collapsed_names.append(letter_part + number_part)

    return collapsed_names


def format_liq_vol(vol_ul: float) -> str:
    if vol_ul >= 1e3:
        return f"{vol_ul/1e3:.2f} mL"
    return f"{vol_ul:.1f} μL"


def add_ser_no_to_log_path(path: str, ser_no: int) -> str:
    dirname, filename = os.path.split(path)
    filename_pieces = filename.rsplit(".", 1)
    basename, ext = \
        filename_pieces \
        if len(filename_pieces) > 1 \
        else (filename_pieces[0], None)
    new_filename = f"{basename}.{ser_no:03d}" \
        + ("" if ext is None else f".{ext}")
    return os.path.join(dirname, new_filename)


def main(proto_file=None, proto_path=None, test_conds=None, prepend_text="",
         runlog_output_path=None, quiet: bool = False,
         stop_on_protocol_abort: bool = True):
    if proto_file is None:
        if proto_path is None:
            raise ValueError(
                "either proto_file or proto_path must be specified")
        with open(proto_path, "r") as proto_file:
            return main(proto_file=proto_file, proto_path=proto_path,
                        test_conds=test_conds, prepend_text=prepend_text,
                        runlog_output_path=runlog_output_path,
                        quiet=quiet,
                        stop_on_protocol_abort=stop_on_protocol_abort)

    if test_conds is None:
        test_conds = {}
    cond_combos = [
            dict(x) for x in itertools.product(*[
                itertools.product((key,), values)
                for (key, values) in test_conds.items()
            ])
        ]
    n_warn_runs = 0
    n_aborted_runs = 0
    analyzers = {}
    # ^ TODO If we're not actually doing anything with this, get rid of it
    for run_idx, conds in enumerate(cond_combos):
        if runlog_output_path is not None and len(cond_combos) > 1:
            runlog_output_path_ser = \
                add_ser_no_to_log_path(runlog_output_path, run_idx)
        else:
            runlog_output_path_ser = runlog_output_path
        if conds:
            conds_desc = "; ".join(
                f"{key}={val}" for (key, val) in conds.items())
        else:
            conds_desc = "(none)"
        print(f"Analyzing with conditions: {conds_desc}")
        proto_file.seek(0)
        try:
            pa = ProtocolAnalyzer.from_protocol_file(
                proto_file,
                plate_slots=PLATE_SLOTS,
                prepend_text=prepend_text,
                add_vars=conds, 
                quiet=quiet
                )
        except ProtocolScriptAborted as e:
            print(
                "\n\nProtocol script aborted! "
                "Traceback follows.",
                file=sys.stderr
                )
            for line in e.traceback_lines:
                print(line, file=sys.stderr)
            n_aborted_runs += 1
            if stop_on_protocol_abort:
                break
            continue

        multi_use_tips = [
            loc for (loc, n_uses) in pa.tip_n_uses.items() if n_uses > 1]
        multi_swap_boxes = [
            slot for (slot, n_swaps) in pa.rack_n_swaps.items() if n_swaps > 1]
        untouched_boxes = [
            slot_name
            for slot_name, tips_used in pa.tracker.tips_used.items()
            if not tips_used]
        used_box_slots = {
            loc[0] for (loc, n_uses) in pa.tip_n_uses.items()}
        swapped_boxes = [
            slot for (slot, n_swaps) in pa.rack_n_swaps.items() if n_swaps > 0]
        liquid_contents = [
            (slot_no, pa.tracker.get_slot_contents(slot_no))
            for slot_no in pa.tracker.get_slot_nos()]
        labware_names = {
            slot_no: pa.tracker.labware_names[slot_no]
            for (slot_no, _) in liquid_contents}

        print("\nLiquid tracking:")
        if not len(liquid_contents):
            print("  (no liquid contents tracked)")
        else:
            print("  Final state:")
        for (slot_no, slot_contents) in liquid_contents:
            print(f"    Slot {slot_no}"
                  + (
                        f" ({labware_names[slot_no]})"
                        if slot_no in labware_names
                        else "")
                  + ":")
            well_names = list(slot_contents.keys())
            groups = []
            while well_names:
                well_name = well_names[0]
                matching_wells = [
                    x for x in well_names
                    if slot_contents[x] == slot_contents[well_name]]
                groups.append((matching_wells, slot_contents[well_name]))
                well_names[:] = [
                    x for x in well_names if x not in matching_wells]
            for (well_names, well_contents) in groups:
                wells_desc = ", ".join(collapse_well_names(well_names))
                s_or_not = "s" if len(well_names) > 1 else " "
                print(f"      Well{s_or_not} {wells_desc}: "
                      f"{format_liq_vol(well_contents.get_total_vol())} ("
                      + ", ".join(
                            f"{format_liq_vol(vol)} {name}"
                            for (name, vol)
                            in well_contents.get_constituents())
                      + ")")

        print("\nSanity checker warnings:")
        if not multi_use_tips and not multi_swap_boxes and not untouched_boxes:
            print("  None!")
        else:
            n_warn_runs += 1
        if multi_use_tips:
            print("  Tip locations picked up multiple times "
                  f"({len(multi_use_tips)}): "
                  + ", ".join(format_loc(x) for x in multi_use_tips))
        if multi_swap_boxes:
            print("  Tipracks swapped multiple times "
                  f"({len(multi_swap_boxes)}): "
                  + ", ".join(f"slot {x}" for x in multi_swap_boxes))
        if untouched_boxes:
            print(f"  Tipracks not used ({len(untouched_boxes)}): "
                  + ", ".join(f"slot {x}" for x in untouched_boxes))
        print("Summary info:")
        print(f"  Number of runlog steps: {pa.n_steps}")
        print(f"  Tipracks accessed ({len(used_box_slots)}): "
              + ", ".join(f"slot {slot}" for slot in used_box_slots))
        if swapped_boxes:
            print(f"  Tipracks swapped ({len(swapped_boxes)}): "
                  + ", ".join(f"slot {slot}" for slot in swapped_boxes))
        if used_box_slots:
            print(f"  Number of tips used:")
            for slot_name, tips_used in pa.tracker.tips_used.items():
                if tips_used:
                    print(f"    slot {slot_name:3s}: {len(tips_used)}")

        if pa.est_duration_s is not None:
            print(f"  Estimated duration: {pa.est_duration_s / 60.:.1f} min")

        if runlog_output_path_ser is not None:
            with open(runlog_output_path_ser, "w") as outf:
                outf.write(f"# Input path: {proto_path!r}\n#\n")
                outf.write("# Test conditions:\n")
                for key in sorted(conds.keys()):
                    outf.write(f"# {key}={conds[key]}\n")
                outf.write("#\n")
                for line in pa.runlog_lines:
                    outf.write(line + "\n")
            print(f"Wrote Opentrons runlog to {runlog_output_path_ser!r}")

        analyzers[tuple(conds.items())] = pa

    print(f"\nTotal number of parameter combinations: {len(cond_combos)}"
          f"\nNumber of runs with warnings: {n_warn_runs}"
          f"\nNumber of aborted runs: {n_aborted_runs}"
          + (" (testing halted)" if n_aborted_runs else "")
          )
