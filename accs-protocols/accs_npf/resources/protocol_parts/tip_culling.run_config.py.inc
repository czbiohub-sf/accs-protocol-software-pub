# Run config for tip culling

run_config = {
    'logging': {
        'log_dir': "/var/lib/jupyter/notebooks/run_logs/",
        'run_id': "tip_culling",
        'run_info': {
        }
    },
    'equipment': {
        'tiprack_def_1ch': "opentrons_96_filtertiprack_200ul",
    },
    'tip_culling': {
        'wells_to_cull': [],
        'tiprack_slots': [1, 2, 4, 5, 7, 8, 10, 11]
    }
}
