metadata = {
    'protocolName': "Fixed-factor Splitting",
    'description': "Splits a source plate (containing adherent cells) to "
                   "a stock plate, aliquoting according to specified split "
                   "factors for each well, then optionally transfers a "
                   "portion of the stock plate contents to a second plate.",
    'apiLevel': '2.9'
}

ps_class = CellSplitting


def ps_run(p: CellSplitting):
    dupe_plate_exists = p.dest_plate_exists('dupe_plate')
    cherryskipping_mode = p.get_splitting_param('cherryskipping_mode')
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

    # Schedule a tipbox swap if we are in "cherryskipping mode"
    if cherryskipping_mode:
        p.arm_tiprack_reset([5])

    # Set the tempdeck for incubation
    p.set_tempdeck(stage='warm')

    # Wash and trypsinize first two sets
    for i in (0, 1):
        if not p.col_set_exists(i):
            break
        p.do_prequench_steps(i)
    # Quench first set
    p.quench(0, mix_after=cherryskipping_mode)
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
        p.quench(1, mix_after=cherryskipping_mode)

    # Shut off tempdeck to start cooling down -- unfortunately the current
    # Opentrons API doesn't provide a method to just change the setpoint,
    # as opposed to waiting for the tempdeck to reach the new temperature
    if not p.col_set_exists(2):
        p.set_tempdeck(stage='coast')

    col_nos = p.get_col_nos()
    for (col_idx, col_no) in enumerate(col_nos):
        # If we are not cherryskipping, triturate with the multichannel
        # first to save some time. Otherwise it gets done as part of
        # the quench() step.
        if not cherryskipping_mode:
            p.pick_up_tips('8ch')
            p.do_tilted_mix_8ch(col_no=col_no, stage='postquench')
            p.drop_tips('8ch')

        # Calculate aliquot volumes for destination plate(s) based on
        # user supplied split factors
        aliquot_vols = p.calc_aliquots_by_split(
            'stock_plate', col_no, dupe_plates=dupe_plates)

        # Transfer 1 column worth of cell aliquots from source plate to
        # first destination plate
        for well_name in aliquot_vols:
            p.aliquot_from_source_plate(
                dest_name='stock_plate',
                well_name=well_name,
                vol=aliquot_vols[well_name],
                dupe_plates=dupe_plates,
                cci=False
            )

            # After doing the first 4 aliquots, stop and quench 3rd set
            if (
                    well_name[0] == 'D'
                    and p.col_set_exists(2)
                    and not p.col_set_quenched(2)):
                p.quench(2, mix_after=cherryskipping_mode)
                p.set_tempdeck(stage='coast')

        # Set tempdeck to hold 25°C -- hopefully after 3 columns' worth of
        # aliquots it has coasted down enough
        if col_idx == 2:
            p.set_tempdeck(stage='rt')

    # Transfer a portion over into the dupe plate if applicable
    if p.dest_plate_exists('dupe_plate'):
        p.transfer_to_dupe(from_name='stock_plate', dupe_name='dupe_plate')

    # Pull tempdeck down to 25°C then shut it off
    p.set_tempdeck(stage='rt')
    p.set_tempdeck(stage='coast')
