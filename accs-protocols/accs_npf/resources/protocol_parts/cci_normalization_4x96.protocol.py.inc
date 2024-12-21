metadata = {
    "protocolName": "4x96 CCI Normalization",
    "description": ("Splits one 96-well-plate to one glass imaging plate, one "
                    "backup plate, one plate for freezing, and one plate for "
                    "sequencing. The secondary plates will default to take the "
                    "rest of the fluid in the source plate wells if it can not "
                    "reach the desired number of cells."),
    "apiLevel": "2.9"
}


class CciCellSplitting4x96(CciCellSplitting):
    def init_labware(self):
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

        self.cci_flow_cell = self.load_labware(
            slot_no=self.get_splitting_param('cci_slot'),
            def_name=self.get_splitting_param('cci_def'),
            name='cci'
            )
        self.cci_approach_loc = \
            self.cci_flow_cell.wells_by_name()['A1'].top(10.)
        self.cci_loading_loc = \
            self.cci_flow_cell.wells_by_name()['A1'].top(-5.)

    def load_destination_plates(self):
        self.dest_plates = OrderedDict()
        for plate_name in self.get_splitting_param('dest_plates'):
            self.dest_plates[plate_name] = self.load_labware(
                self.get_splitting_param('dest_plate_defs', plate_name),
                self.get_splitting_param('dest_plate_slots', plate_name),
                name=plate_name)


ps_class = CciCellSplitting4x96


def ps_run(p: CciCellSplitting4x96):
    """ CCI-normalized splitting to four 96-well plates

    The challenge here is managing deckspace. After the trash,
    trough, CCI, and source, we have 8 slots left for (at least)
    10 (plates + tipracks).

    We get around this by pre-quenching, quenching, and doing
    CCI counts before swapping the deck for the plates and adding
    new tipracks.

    Before the swap, we use 31 columns (for prequench
    & quench) + 24 columns (for CCI counts) = 55 cols = 5 boxes, 7 cols
    = 6 boxes req'd.

    After the swap, we place 4 boxes in back 4 slots, and the 4 plates
    in the front 4 slots.
    """
    """
    STAGE 1:
        Get all cell counts

    This is relatively standard to what we do in other files.
    """
    # Initial deck setup
    # We do not need any single channel tipboxes
    # until after the deck swap
    initial_tiprack_slots = [1, 2, 4, 5, 7]
    p.load_tipracks(
        slot_nos=initial_tiprack_slots,
        pipette='8ch'
    )

    # Check CCI comms and start a new data file,
    # prime the flow cell and take background images
    p.init_cci()
    p.pick_up_tips('8ch')
    p.prime_cci()
    p.do_cci_bg_scan()
    p.drop_tips('8ch')

    # Set the tempdeck for incubation
    p.set_tempdeck(stage='warm')

    # Wash and trypsinize first two sets
    p.do_prequench_steps(0)
    p.do_prequench_steps(1)

    # Quench first set
    p.quench(0)

    p.do_prequench_steps(2)

    # Quench second set
    p.quench(1)

    # Shut off tempdeck to start cooling down -- unfortunately the current
    # Opentrons API doesn't provide a method to just change the setpoint,
    # as opposed to waiting for the tempdeck to reach the new temperature

    p.set_tempdeck(stage='coast')

    plate_densities = []

    col_nos = p.get_col_nos()
    for col_no in col_nos:
        # Pick up a set of tips
        p.pick_up_tips('8ch')

        # Triturate to singularize cells
        p.do_tilted_mix_8ch(col_no=col_no, stage='postquench')

        # Take sample from source plate and load into flow cell
        p.load_cci_from_source_plate(col_no)

        # Request counting scan and get results
        well_densities = p.do_cci_counting_scan(col_no)
        plate_densities.append(well_densities)

        # Unplug the tips from the flow cell
        p.pull_out_of_cci()

        # Discard tips
        p.drop_tips('8ch')

        # Flush CCI
        p.flush_cci(stage='between')

        # Set tempdeck to hold 25°C -- hopefully after 3 columns' worth of
        # aliquots it has coasted down enough
        if col_no == 3:
            p.set_tempdeck(stage='rt')

        # After doing the first 4 aliquots, stop and quench 3rd set
        # TODO Verify the timing of this in the protocol is correct
        if col_no == 4 \
                and (not p.col_set_quenched(2)) \
                and p.col_set_quench_ready(2):
            p.quench(set_idx=2)
            p.set_tempdeck(stage='coast')

    # Treat flow cell with detergent
    p.flush_cci(stage='wash')

    # Flush flow cell with water
    p.flush_cci(stage='rinse')

    # Set tempdeck to hold 25°C if we haven't already
    p.set_tempdeck(stage='rt')

    """
    STAGE 2:
        Deckswap
    """
    p.unload_tipracks(initial_tiprack_slots)

    p.pause_with_message(
        "Remove all tipracks and then place tipracks in slots "
        "7, 8, 10, and 11. Place the glass-bottomed cellview 96-wellplate "
        "440ul plate on slot 5, and regular corning 96 wellplate 360ul flat "
        "plates on slots 1, 2, and 4."
    )

    p.load_destination_plates()

    p.load_tipracks(
        slot_nos=[7],
        pipette='8ch'
    )

    p.load_tipracks(
        slot_nos=[8, 10, 11],
        pipette='1ch'
    )

    """
    STAGE 3:
        Plate prefills

    We do calculations here, along with prefilling the plates.
    """
    well_names = [well.well_name for well in p.source_plate.wells()]
    pretransfer_vol = {
        well_name: p.get_pretransfer_vol(well_name)
        for well_name in well_names
    }

    glass_plate_aliquot_vols_by_col = {
        col_no: p.calc_aliquots_by_density(well_density_col, "glass_plate")
        for col_no, well_density_col in enumerate(plate_densities, start=1)
    }

    # the backup, freeze, and seq plates have the same amount of cells
    secondary_plates_aliquot_vols_by_col = {
        col_no: p.calc_aliquots_by_density(well_density_col, "backup_plate", target_vol=135)
        for col_no, well_density_col in enumerate(plate_densities, start=1)
    }

    # Maximum of remaining volume per well - this  is used for the prefill
    remaining_vols = {
        col_no: {
            well_name: pretransfer_vol[well_name] - aliquot_vol
            for well_name, aliquot_vol in aliquots.items()
        }
        for col_no, aliquots in glass_plate_aliquot_vols_by_col.items()
    }

    dupe_plates = p.get_splitting_param("dest_plates")
    dupe_plates.remove("glass_plate")

    # use 8ch for time - can hop to 1ch if we care about volume more than time
    p.pipette_8ch.pick_up_tips()
    for plate_name in dupe_plates:
        prefil_vol = p.get_splitting_param("plate_prefill_vols", plate_name)
        p.prefill_dest_plate(
            dest_name=plate_name,
            vol=prefil_vol,
            col_nos=p.get_col_nos(),
            handle_tips=False
        )
    p.pipette_8ch.drop_tips()

    """
    STAGE 4:
        ALIQUOTS
    """
    for col_no, column in enumerate(p.source_plate.columns(), start=1):
        # Transfer 1 column worth of cell aliquots from source plate to
        # first destination plate
        for well in column:
            well_name = well.well_name
            p.aliquot_from_source_plate(
                dest_name='glass_plate',
                well_name=well_name,
                vol=glass_plate_aliquot_vols_by_col[col_no][well_name]
            )
            p.pipette_1ch.pick_up_tip()

            remaining_vol = remaining_vols[col_no][well_name]
            desired_vol = 3 * secondary_plates_aliquot_vols_by_col[col_no][well_name]
            if desired_vol <= remaining_vol:
                vol = desired_vol
            else:
                vol = remaining_vol / 3
                p.log_notice(
                    f"Not enough fluid to reach desired number of cells in the secondary "
                    f"plates. Aliquoting {vol} µL."
                )

            # most likely due to cell counts / volumes at this point, we will only
            # be in the one_aspirate case. Verify w/ ML
            p.do_tilted_mix(pipette=p.pipette_1ch, well_name=well_name,
                              stage=p.TiltedMixStage.PRETRANSFER, well_vol=remaining_vol)
            p.pipette_1ch.aspirate(
                vol,
                p.get_source_well_bottom_corner_loc(well_name)
            )

            for plate_name in dupe_plates:
                p.pipette_1ch.dispense(
                    vol / 3,
                    p.dest_plates[plate_name].wells_by_name()[well_name]
                )

            p.pipette_1ch.drop_tip()
