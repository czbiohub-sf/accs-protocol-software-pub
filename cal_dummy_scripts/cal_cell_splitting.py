# Dummy protocol script for labware calibration for CCI-based protocols
metadata = {
    'protocolName': "Labware calibration dummy script for cell splitting",
    'description': "Load this dummy script to do labware calibration for cell "
                   "splitting protocols (cci_normalization, cci_presusp_norm, "
                   "cci_count_only, fixed_splitting, etc.). You only need to "
                   "install and calibrate the particular labware type(s) you "
                   "are actually using (i.e. you can skip calibrating to the "
                   "CCI if you are not running a CCI protocol). "
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
    cci = ctx.load_labware_by_name('cci25_right', 6)
    tempdeck = ctx.load_module('tempdeck', 3)
    plate = tempdeck.load_labware_by_name('tilted_plate')
    propagation_plate = ctx.load_labware_by_name(
        'corning_96_wellplate_360ul_flat', 2)
    microscopy_plate = ctx.load_labware_by_name(
        'cellview_96_wellplate_440ul', 1)

    pipette.pick_up_tip()
    for labware in (trough, cci, plate, propagation_plate, microscopy_plate):
        pipette.aspirate(1e-12, labware.wells()[0].top(25.))
    pipette.return_tip()
