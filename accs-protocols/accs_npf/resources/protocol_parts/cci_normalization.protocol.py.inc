metadata = {
    'protocolName': "CCI normalization",
    'description': "Splits a source plate (containing adherent cells) to "
                   "a stock plate, using the CCI to normalize to specified "
                   "cell count targets for each well, then optionally "
                   "transfers a portion of the stock plate contents to a "
                   "second plate.",
    'apiLevel': '2.9'
}

ps_class = CciCellSplitting


def ps_run(p: CciCellSplitting):
    dupe_plate_exists = p.dest_plate_exists('dupe_plate')
    dupe_plates = ['dupe_plate'] if dupe_plate_exists else ()

    # Initial deck setup
    p.load_tipracks(
        slot_nos=[5, 8, 4, 7, 10],
        pipette='8ch'
        )
    p.load_tipracks(
        slot_nos=[11],
        pipette='1ch'
        )
    # Set tipbox on slot 5 to be hot-swapped if we're making a dupe plate
    if dupe_plate_exists:
        p.arm_tiprack_reset(slot_nos=[5])

    # Check CCI comms and start a new data file
    p.init_cci()

    # Set the tempdeck for incubation
    p.set_tempdeck(stage='warm')

    # Prime the flow cell and take background images
    p.pick_up_tips('8ch')
    p.prime_cci()
    p.do_cci_bg_scan()
    p.drop_tips('8ch')

    # Flush CCI
    p.flush_cci(stage='between')

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
        # Pick up a set of tips
        p.pick_up_tips('8ch')

        # Triturate to singularize cells
        p.do_tilted_mix_8ch(col_no=col_no, stage='postquench')

        # Take sample from source plate and load into flow cell
        p.load_cci_from_source_plate(col_no)

        # Request counting scan and get results
        well_densities = p.do_cci_counting_scan(col_no)
        p.update_results('source_cell_density', well_densities)

        # Calculate aliquot volumes for destination plate(s) based on cell
        # count targets
        aliquot_vols = p.calc_aliquots_by_density(
            well_densities, 'stock_plate', dupe_plates=dupe_plates)
        p.update_results('aliquot_vol', aliquot_vols, write_now=True)

        # Unplug the tips from the flow cell
        p.pull_out_of_cci()

        # Discard tips
        p.drop_tips('8ch')

        # Flush CCI
        p.flush_cci(stage='between')

        # Transfer 1 column worth of cell aliquots from source plate to
        # first destination plate
        for well_name in well_densities:
            p.aliquot_from_source_plate(
                dest_name='stock_plate',
                well_name=well_name,
                vol=aliquot_vols[well_name],
                dupe_plates=dupe_plates
            )

            # After doing the first 4 aliquots, stop and quench 3rd set
            if well_name[0] == 'D' \
                    and (not p.col_set_quenched(2)) \
                    and p.col_set_quench_ready(2):
                p.quench(set_idx=2)
                p.set_tempdeck(stage='coast')

        # Transfer a portion over into the dupe plate if applicable
        if p.dest_plate_exists('dupe_plate'):
            p.transfer_to_dupe(from_name='stock_plate', dupe_name='dupe_plate',
                               col_nos=[col_no])

        # Set tempdeck to hold 25°C -- hopefully after 3 columns' worth of
        # aliquots it has coasted down enough
        if col_idx == 2:
            p.set_tempdeck(stage='rt')

    # Treat flow cell with detergent
    p.flush_cci(stage='wash')

    # Flush flow cell with water
    p.flush_cci(stage='rinse')

    # Pull tempdeck down to 25°C then shut it off
    p.set_tempdeck(stage='rt')
    p.set_tempdeck(stage='coast')
