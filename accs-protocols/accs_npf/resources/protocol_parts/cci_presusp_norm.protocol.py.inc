metadata = {
    'protocolName': "CCI normalization from suspension",
    'description': "Splits a source plate (containing suspended cells) to "
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

    # Check CCI comms and start a new data file
    p.init_cci()

    # Set the tempdeck to hold room temp
    p.set_tempdeck(stage='rt')

    # Prime the flow cell and take background images
    p.pick_up_tips('8ch')
    p.prime_cci()
    p.do_cci_bg_scan()
    p.drop_tips('8ch')

    # Flush CCI
    p.flush_cci(stage='between')

    # Prefill the first plate with media if we're making a dupe plate
    if dupe_plate_exists:
        prefill_vol = p.get_dupe_prefill_vol(
            from_name='stock_plate', dupe_plates=dupe_plates)
        p.prefill_dest_plate(
            'stock_plate', vol=prefill_vol)

    col_nos = p.get_col_nos()
    for (col_idx, col_no) in enumerate(col_nos):
        # Pick up a set of tips
        p.pick_up_tips('8ch')

        # Triturate to singularize cells
        p.do_tilted_mix_8ch(col_no=col_no, stage='fromsusp')

        # Take sample from source plate and load into flow cell
        p.load_cci_from_source_plate(col_no)

        # Request counting scan and get results
        well_densities = p.do_cci_counting_scan(col_no)

        # Calculate aliquot volumes for destination plate(s) based on cell
        # count targets
        aliquot_vols = p.calc_aliquots_by_density(
            well_densities, 'stock_plate', dupe_plates=dupe_plates,
            presusp=True)

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
                dupe_plates=dupe_plates,
                presusp=True)

    # Transfer a portion over into the dupe plate if applicable
    if p.dest_plate_exists('dupe_plate'):
        p.transfer_to_dupe(from_name='stock_plate', dupe_name='dupe_plate')

    # Treat flow cell with detergent
    p.flush_cci(stage='wash')

    # Flush flow cell with water
    p.flush_cci(stage='rinse')
