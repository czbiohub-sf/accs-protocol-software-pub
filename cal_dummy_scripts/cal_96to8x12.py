# Dummy protocol script for labware calibration for CCI-based protocols
metadata = {
    'protocolName': "Labware calibration dummy script for 96-to-8x12 Expansion",
    'description': "Load this dummy script to do labware calibration for the"
                    "96-to-8x12 Expansion protocol."
                   "FOR LABWARE CALIBRATION ONLY -- do not "
                   "actually run this as a protocol. Nothing useful will "
                   "result.",
    'apiLevel': '2.9'
    }


def run(ctx):
    tiprack = ctx.load_labware_by_name('opentrons_96_filtertiprack_200ul', 5)
    pipette = ctx.load_instrument(
        'p300_multi_gen2', mount='right', tip_racks=[tiprack])

    trough = ctx.load_labware_by_name('usascientific_12_reservoir_22ml', 9)
    tempdeck = ctx.load_module('tempdeck', 3)
    plate = tempdeck.load_labware_by_name('tilted_plate')
    twelvewp = ctx.load_labware_by_name('corning_12_wellplate_6.9ml_flat', 2)

    pipette.pick_up_tip()
    for labware in (trough, plate, twelvewp):
        pipette.aspirate(1e-12, labware.wells()[0].top(25.))
    pipette.return_tip()
