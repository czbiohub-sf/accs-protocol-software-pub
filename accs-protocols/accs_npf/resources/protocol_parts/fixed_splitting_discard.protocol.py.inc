metadata = {
    'protocolName': "Discard Splitting",
    'description': "Splits a source plate (containing adherent cells) "
                   "\"in-place\", i.e. by discarding a portion of cell "
                   "suspension, according to specified split factors.",
    'apiLevel': '2.9'
}

ps_class = CellSplitting


def ps_run(p: CellSplitting):
    cherryskipping_mode = p.get_splitting_param('cherryskipping_mode')

    # Initial deck setup
    p.load_tipracks(
        slot_nos=[1, 4, 5, 7, 8],
        pipette='8ch'
        )
    p.load_tipracks(
        slot_nos=[10, 11],
        pipette='1ch'
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

        # Discard tips
        p.drop_tips('8ch')

        # Calculate desired volumes of cell suspension to leave in each
        # source well
        cell_vols = p.calc_aliquots_by_split('source_plate', col_no,
                                             split_in_place=True)

        # Transfer 1 column worth of cell aliquots from source plate to
        # first destination plate
        for well_name in cell_vols:
            p.discard_and_backfill_source_well(
                well_name=well_name,
                vol=cell_vols[well_name],
            )

            # After doing the first 4 aliquots, stop and quench 3rd set
            # if applicable
            if (
                    well_name[0] == 'D'
                    and p.col_set_exists(2)
                    and not p.col_set_quenched(2)):
                p.quench(2)
                p.set_tempdeck(stage='coast')

        # Set tempdeck to hold 25°C -- hopefully after 3 columns' worth of
        # aliquots it has coasted down enough
        if col_idx == 2:
            p.set_tempdeck(stage='rt')

    # Pull tempdeck down to 25°C then shut it off
    p.set_tempdeck(stage='rt')
    p.set_tempdeck(stage='coast')
