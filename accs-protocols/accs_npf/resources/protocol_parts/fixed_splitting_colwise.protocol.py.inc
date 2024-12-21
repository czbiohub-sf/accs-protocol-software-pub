metadata = {
    'protocolName': "Columnwise Quick-Split",
    'description': "Splits a source plate (containing adherent cells) to "
                   "a stock plate a column at a time, aliquoting according "
                   "to specified split factors for each well, then optionally "
                   "transfers a portion of the main output plate's contents "
                   "to a second plate.",
    'apiLevel': '2.9'
}

ps_class = CellSplitting


def ps_run(p: CellSplitting):
    dupe_plate_exists = p.dest_plate_exists('dupe_plate')
    dupe_plates = ['dupe_plate'] if dupe_plate_exists else ()

    # Initial deck setup
    p.load_tipracks(
        slot_nos=[8, 11, 7, 10, 4],
        pipette='8ch'
        )

    # Set the tempdeck for incubation
    p.set_tempdeck(stage='warm')

    # Wash and trypsinize first two sets
    for i in (0, 1):
        if not p.col_set_exists(i):
            break
        p.do_prequench_steps(i)

    # Quench first set
    p.quench(0)

    # Wash and trypsinize third set
    if p.col_set_exists(2):
        p.do_prequench_steps(2)

    # Prefill the first plate with media if we're making a dupe plate
    if dupe_plate_exists:
        prefill_vol = p.get_dupe_prefill_vol(
            from_name='stock_plate', dupe_plates=dupe_plates)
        p.prefill_dest_plate(
            'stock_plate', vol=prefill_vol)

    # Quench second set
    if p.col_set_exists(1):
        p.quench(1)

    # Shut off tempdeck to start cooling down -- unfortunately the current
    # Opentrons API doesn't provide a method to just change the setpoint,
    # as opposed to waiting for the tempdeck to reach the new temperature
    if not p.col_set_exists(2):
        p.set_tempdeck(stage='coast')

    col_nos = p.get_col_nos()
    for (col_idx, col_no) in enumerate(col_nos):
        well_name = f'A{col_no}'

        # Calculate aliquot volumes for destination plate(s) based on
        # user supplied split factors
        aliquot_vol = p.calc_aliquot_by_split(
            'stock_plate', well_name, dupe_plates=dupe_plates)

        # Transfer 1 column worth of cell aliquots from source plate to
        # first destination plate
        p.aliquot_from_source_plate(
            dest_name='stock_plate',
            well_name=well_name,
            vol=aliquot_vol,
            dupe_plates=dupe_plates,
            pipette='8ch',
            mix_stage='postquench',
            cci=False
        )

        # After doing the first 4 aliquots, stop and quench 3rd set
        if (col_idx >= 3
                and (not p.col_set_quenched(2))
                and p.col_set_quench_ready(2)):
            p.quench(2)
            p.set_tempdeck(stage='coast')
        # Set tempdeck to hold 25°C -- hopefully it has coasted down enough
        # since quenching the 3rd set and turning it off
        elif col_idx == 6:
            p.set_tempdeck(stage='rt')

    # Transfer a portion over into the dupe plate if applicable
    if p.dest_plate_exists('dupe_plate'):
        p.transfer_to_dupe(from_name='stock_plate', dupe_name='dupe_plate')

    # Pull tempdeck down to 25C and then shut it off
    p.set_tempdeck(stage='rt')
    p.set_tempdeck(stage='coast')
