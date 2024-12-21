from typing import Tuple, List


metadata = {
    'protocolName': "96-to-3x96 triplication for mass spec",
    'description': "Expands a 96-well plate of adherent cells into "
                   "3x 96-well output plates: ABC->AAA,BBB,CCC",
    'apiLevel': '2.9'
}


ps_class = CellSplitting


def ps_run(p: CellSplitting):
    # Initial deck setup
    p.load_tipracks(
        slot_nos=[8, 11, 7, 10],
        pipette='8ch'
        )

    # Set the tempdeck for incubation
    p.set_tempdeck(stage='warm')

    # Prefill output plates
    for plate_name in p.get_splitting_param('dest_plates'):
        final_vol = p.get_splitting_param('dest_target_vols')[plate_name]
        aliquot_vol = p.get_splitting_param('aliquot_vols')[plate_name]
        prefill_vol = max(0., final_vol - aliquot_vol)
        p.prefill_dest_plate(
            dest_name=plate_name,
            vol=prefill_vol,
            col_nos=range(1, 13),
            )

    # Wash, tryspinize and quench the source plate
    p.do_prequench_steps(0)
    p.do_prequench_steps(1)
    p.quench(0)
    p.do_prequench_steps(2)
    p.quench(1)
    p.quench(2)

    # Shut off tempdeck to start cooling down
    p.set_tempdeck(stage='coast')

    # Transfer
    for col_no in p.get_col_nos():
        dest_plate_idx = (col_no-1) // 4
        dest_plate = p.get_splitting_param('dest_plates')[dest_plate_idx]
        dest_col_start = col_no - 4 * dest_plate_idx
        dest_cols = [dest_col_start + 4 * x for x in range(3)]
        aliquot_vol = p.get_splitting_param('aliquot_vols')[dest_plate]
        total_asp_vol = 3. * aliquot_vol

        p.comment(f"Distributing {total_asp_vol:.1f}μL from col {col_no} "
                  f"on source plate to cols {dest_cols} on {dest_plate!r}",
                  also_log=True)
        p.pick_up_tips('8ch')
        p.do_tilted_mix_8ch(col_no=col_no, stage='postquench')
        p.aspirate(
            '8ch',
            vol=total_asp_vol,
            location=p.get_source_well_bottom_corner_loc(f'A{col_no}'))
        for i, dest_col_no in enumerate(dest_cols):
            dest_well = p.dest_plates[dest_plate].wells_by_name()[
                f'A{dest_col_no}']
            if i > 0:
                p.dispense('8ch', vol=5., location=dest_well.top(),
                           in_air=True)
            p.dispense('8ch', vol=aliquot_vol, location=dest_well)
            p.comment(f"Dispensed {aliquot_vol} into col {dest_col_no} "
                      f"on {dest_plate!r}")
            p.aspirate('8ch',
                       vol=5. if i < (len(dest_cols) - 1) else 20.,
                       location=dest_well.top(),
                       in_air=True)
        p.drop_tips('8ch')

    # Set tempdeck to cool the rest of the way to 25°C, then turn it off
    p.set_tempdeck(stage='rt')
    p.set_tempdeck(stage='coast')
