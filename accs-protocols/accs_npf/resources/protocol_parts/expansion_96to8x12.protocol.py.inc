from typing import Tuple, List


metadata = {
    'protocolName': "96-to-8x12 Expansion",
    'description': "Passages a 96-well plate of adherent cells into "
                   "eight 12-well plates that are prefilled with media",
    'apiLevel': '2.9'
}


class ExpansionSplitting(CellSplitting):
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

    def load_destination_plates(self):
        self.dest_plates = OrderedDict()
        for plate_name in self.get_splitting_param('dest_plates'):
            self.dest_plates[plate_name] = self.load_labware(
                self.get_splitting_param('dest_plate_defs', plate_name),
                self.get_splitting_param('dest_plate_slots', plate_name),
                name=plate_name)

    def map_source_well_to_12wp_and_well(self, well_name: str
                                         ) -> Tuple[str, str]:
        """
        Aliquot mapping

        - each row of source gets a plate to itself
        - wells in each row map out in Z pattern onto the plate
        """
        source_row, source_col = well_name[0].upper(), int(well_name[1:])

        dest_name = self.get_splitting_param('dest_plates')[
            "ABCDEFGH".index(source_row)]

        dest_row_int = (source_col - 1) // 4
        dest_row = "ABC"[dest_row_int]
        dest_col = (source_col - 1) % 4 + 1
        dest_well = f"{dest_row}{dest_col}"

        return dest_name, dest_well


ps_class = ExpansionSplitting


def ps_run(p: ExpansionSplitting):
    # Initial deck setup
    eight_ch_tiprack_slots = [1, 2, 4, 5]

    p.load_tipracks(
        slot_nos=eight_ch_tiprack_slots,
        pipette='8ch'
        )
    p.load_tipracks(
        slot_nos=[6],
        pipette='1ch'
        )

    # Set the tempdeck for incubation
    p.set_tempdeck(stage='warm')

    # Wash, tryspinize and quench the source plate...
    p.do_prequench_steps(0)
    p.do_prequench_steps(1)
    p.quench(0)
    p.do_prequench_steps(2)
    # We need to do these two before we swap the deck,
    # since the deck will soon be filled with plates.
    p.quench(1)
    p.quench(2)

    # Shut off tempdeck to start cooling down
    p.set_tempdeck(stage='coast')

    # Triturate with the multichannel to singularize the cells
    for col_no in p.get_col_nos():
        p.pick_up_tips('8ch')
        p.do_tilted_mix_8ch(col_no=col_no, stage='postquench')
        p.drop_tips('8ch')

    # Prompt user to swap out labware and then reset the state of the deck
    # for the second phase
    dest_plate_slots = [
        p.get_splitting_param('dest_plate_slots')[dest_name]
        for dest_name in p.get_splitting_param('dest_plates')]
    p.pause_with_message(
        f"Remove tipracks from slots {eight_ch_tiprack_slots}, "
        f"then install 12 well plates in slots {dest_plate_slots}.")
    p.unload_tipracks(eight_ch_tiprack_slots)
    p.load_destination_plates()

    # Set tempdeck to cool the rest of the way to 25°C and hold
    p.set_tempdeck(stage='rt')

    # Distribute contents of each source plate column out to the 12wps
    for col_no in p.get_col_nos():
        plate_col = p.source_plate.columns_by_name()[str(col_no)]
        for well_name in (w.well_name for w in plate_col):
            dest_name, dest_well = \
                p.map_source_well_to_12wp_and_well(well_name)

            aliquot_vol = min(
                p.get_tip_capacity('1ch'),
                p.get_postquench_vol(well_name))

            p.aliquot_from_source_plate(
                dest_name=dest_name,
                well_name=well_name,
                vol=aliquot_vol,
                target_vol=aliquot_vol,
                dest_well=dest_well,
                cci=False,
            )

    # Shut off tempdeck
    p.set_tempdeck(stage='coast')
