# Run config for a 96-wp to eight 12-wp expansion

run_config = {
    'logging': {
        'log_dir': "/var/lib/jupyter/notebooks/run_logs/",
        'run_id': "expansion_96to8x12",
        'run_info': {
            'operator': None,
            'source_plate_id': None,
            'comment': None
        }
    },
    'liquid_tracking': {
        'initial_fills': {
            'trough': [
                ('A3', 'pbs', 20.0e3),
                ('A5', 'trypsin', 8.0e3),
                ('A6', 'media', 20.0e3),
                ('A7', 'media', 20.0e3),
                ],
            'source_plate': [
                (f'{row}{col}', 'old_media', 200.)
                for row in "ABCDEFGH" for col in range(1, 13)
                ]
            }
    },
    'cell_splitting': {
        'start_col': 1,
        'end_col': 12,
        'n_cols_per_set': 4,
        'trypsin_vol': 60.,
        'trypsin_time': 60. * 8.,
        'trypsinize_reuse_tips': True,
        'pbs_wash_vol': 100.,
        'pbs_wash_dispense_rate': 20.,
        'pbs_wash_aspirate_rate': 40.,
        'quench_vol': 200.,
        'quench_reuse_tips': True,
        'mix_flow_rate': 1000.,
        'tilted_mix_n_cycles_fromsusp': 10,
        'tilted_mix_n_cycles_postquench': 15,
        'tilted_mix_n_cycles_pretransfer': 4,
        'tilted_mix_plunger_boost_postquench': True,
        'tilted_mix_motor_current_postquench': 0.8,
        'tilted_mix_portion_postquench': 0.4,
        'flat_mix_n_cycles_pretransfer': 1,
        'flat_mix_n_cycles_posttransfer': 1,
        'min_cell_aliquot_vol': 20.,
        'tempdeck_temp_warm': 41.,
        'tempdeck_temp_rt': 25.,
        'tiprack_def_1ch': 'opentrons_96_filtertiprack_200ul',
        'tip_capacity_1ch': 200.,
        'tiprack_def_8ch': 'opentrons_96_filtertiprack_200ul',
        'tip_capacity_8ch': 200.,
        'tempdeck_def': 'tempdeck',
        'tempdeck_slot': 3,
        'source_plate_def': 'tilted_plate',
        'dest_plates': [
            '12wp_1',
            '12wp_2',
            '12wp_4',
            '12wp_5',
            '12wp_7',
            '12wp_8',
            '12wp_10',
            '12wp_11',
            ],
        'dest_plate_defs': {
            '12wp_1': 'corning_12_wellplate_6.9ml_flat',
            '12wp_2': 'corning_12_wellplate_6.9ml_flat',
            '12wp_4': 'corning_12_wellplate_6.9ml_flat',
            '12wp_5': 'corning_12_wellplate_6.9ml_flat',
            '12wp_7': 'corning_12_wellplate_6.9ml_flat',
            '12wp_8': 'corning_12_wellplate_6.9ml_flat',
            '12wp_10': 'corning_12_wellplate_6.9ml_flat',
            '12wp_11': 'corning_12_wellplate_6.9ml_flat'
            },
        'dest_plate_slots': {
            '12wp_1': 1,
            '12wp_2': 2,
            '12wp_4': 4,
            '12wp_5': 5,
            '12wp_7': 7,
            '12wp_8': 8,
            '12wp_10': 10,
            '12wp_11': 11,
            },
        'trough_def': 'usa_12well_22ml_reservoir_lifted_76mm5',
        'trough_slot': 9,
        'trough_wells': {
            'waste':     ['A1', 'A2'],
            'pbs':       ['A3'],
            'trypsin':   ['A5'],
            'media':     ['A6', 'A7']
            },
    }
}
