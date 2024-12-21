# Run config for CCI Normalization

run_config = {
    'logging': {
        'log_dir': "/var/lib/jupyter/notebooks/run_logs/",
        'run_id': "cci_normalization",
        'run_info': {
            'operator': None,
            'flowcell_id': None,
            'source_plate_id': None,
            'stock_plate_id': None,
            'dupe_plate_id': None,
            'comment': None
        }
    },
    'cci': {
        'force_url': None,
        'server_urls': {
            # (ROBOT_HOSTNAME, SERVER_URL),
            } # TODO mirror this to run_configs for other CCI protocols
    },
    'liquid_tracking': {
        'initial_fills': {
            'trough': [
                ('A3', 'pbs', 20.0e3),
                ('A4', 'pbs', 20.0e3),
                ('A5', 'trypsin', 8.0e3),
                ('A6', 'media', 20.0e3),
                ('A7', 'media', 20.0e3),
                ('A8', 'media', 20.0e3),
                ('A9', 'water', 8.0e3),
                ('A10', 'detergent', 4.0e3),
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
        'tilted_mix_n_cycles_fromsusp': 4,
        'tilted_mix_n_cycles_postquench': 15,
        'tilted_mix_n_cycles_pretransfer': 4,
        'tilted_mix_plunger_boost_postquench': True,
        'tilted_mix_motor_current_postquench': 0.8,
        'tilted_mix_portion_postquench': 0.4,
        'flat_mix_n_cycles_pretransfer': 1,
        'flat_mix_n_cycles_posttransfer': 1,
        'cci_loading_vol': 30.,
        'cci_loading_rate': 60.,
        'cci_settling_time': 60.,
        'cci_flush_vol': 200.,
        'cci_prime_rate': 10.,
        'cci_flush_rate': 1000.,
        'cci_wash_n_cycles': 1,
        'cci_wash_dwell_time': 60.,
        'cci_rinse_n_cycles': 3,
        'cci_density_clip_min': 0.,
        'cci_density_clip_max': 2000.,
        'cci_valid_area_warn_thresh': 0.5,
        'min_cell_aliquot_vol': 20.,
        'tempdeck_temp_warm': 41.,
        'tempdeck_temp_rt': 25.,
        'tiprack_def_1ch': 'opentrons_96_filtertiprack_200ul',
        'tip_capacity_1ch': 200.,
        'tiprack_def_8ch': 'opentrons_96_filtertiprack_200ul',
        'tip_capacity_8ch': 200.,
        'tempdeck_def': 'tempdeck',
        'tempdeck_slot': 3,
        'cci_def': 'cci25_right',
        'cci_slot': 6,
        'source_plate_def': 'tilted_plate',
        'dest_plates': ['stock_plate', 'dupe_plate'],
        'dest_plate_defs': {
            'stock_plate': 'corning_96_wellplate_360ul_flat',
            'dupe_plate': 'cellview_96_wellplate_440ul'
            },
        'dest_plate_slots': {
            'stock_plate': 2,
            'dupe_plate': 1
            },
        'dest_target_vols': {
            'stock_plate': 150.,
            'dupe_plate': 150.
            },
        'plate_vol_limits': {
            'stock_plate': 300.,
            },
        'trough_def': 'usa_12well_22ml_reservoir_lifted_76mm5',
        'trough_slot': 9,
        'trough_wells': {
            'waste':     ['A1', 'A2'],
            'pbs':       ['A3', 'A4'],
            'trypsin':   ['A5'],
            'media':     ['A6', 'A7', 'A8'],
            'water':     ['A9'],
            'detergent': ['A10'],
            },
        'target_counts': {
            'stock_plate': {
                f'{row}{col}': 10.0e3
                for row in "ABCDEFGH"
                for col in range(1, 13)
            }
        },
    }
}
