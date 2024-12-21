metadata = {
    'protocolName': "CCI counting only",
    'description': "Samples each column of a source plate (containing "
                   "suspended cells) and uses the CCI to measure cell "
                   "density. Does not propagate to a new plate.",
    'apiLevel': '2.9'
}

ps_class = CciCellSplitting


def ps_run(p: CciCellSplitting):
    # Initial deck setup
    p.load_tipracks(
        slot_nos=[5, 8, 4],
        pipette='8ch'
        )

    # Check CCI comms and start a new data file
    p.init_cci()

    # Set the tempdeck for incubation
    p.set_tempdeck(stage='rt')

    # Prime the flow cell and take background images
    p.pick_up_tips('8ch')
    p.prime_cci()
    p.do_cci_bg_scan()
    p.drop_tips('8ch')

    # Flush CCI
    p.flush_cci(stage='between')

    col_nos = p.get_col_nos()
    for (col_idx, col_no) in enumerate(col_nos):
        # Pick up a set of tips
        p.pick_up_tips('8ch')

        # Triturate to singularize cells
        p.do_tilted_mix_8ch(col_no=col_no, stage='fromsusp')

        # Take sample from source plate and load into flow cell
        p.load_cci_from_source_plate(col_no)

        # Request counting scan and get results
        p.do_cci_counting_scan(col_no)

        # Unplug the tips from the flow cell
        p.pull_out_of_cci()

        # Discard tips
        p.drop_tips('8ch')

        # Flush CCI
        p.flush_cci(stage='between')

    # Treat flow cell with detergent
    p.flush_cci(stage='wash')

    # Flush flow cell with water
    p.flush_cci(stage='rinse')
