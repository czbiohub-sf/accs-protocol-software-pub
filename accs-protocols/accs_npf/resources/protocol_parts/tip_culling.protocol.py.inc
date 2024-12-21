from typing import Dict, Any

metadata = {
    'protocolName': "Tip Culling",
    'description': "Removes a specific set of tips from the deck for the "
                   "purpose of 'cherryskipping' certain wells. NOTE: Each "
                   "tip culling script file corresponds to a specific "
                   "instance of a protocol script generated at the same time.",
    'apiLevel': '2.9'
}


class TipCulling(ProtocolSteps):
    PIPETTE_TYPE_LEFT = 'p300_single_gen2'
    PIPETTE_ALIAS_LEFT = '1ch'


ps_class = TipCulling


def ps_run(p: TipCulling):
    all_tipracks = p.get_run_param("tip_culling", "tiprack_slots")
    wells_to_cull = p.get_run_param("tip_culling", "wells_to_cull")

    tipracks = p.load_tipracks(
        slot_nos=all_tipracks,
        pipette='1ch'
    )

    pipette = p.pipette_1ch

    for (slot, well_name) in wells_to_cull:
        labware = tipracks[int(slot)]
        p.log_debug(f"Removing tip from slot {slot} location {well_name}")
        pipette.pick_up_tip(labware.well(well_name))
        pipette.drop_tip(home_after=False)
