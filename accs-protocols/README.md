# accs-protocols
This package contains the framework for ACCS protocol scripts, a mechanism for generating personalized one-off scripts for each run configured using a setup form, and an analysis tool to aid in protocol development.

## Requirements
See `pyproject.toml`.

## Installing
`accs-protocols` should be installed in a dedicated Python>=3.8 virtual environment. This can be accomplished easily using `pipx` ,
```
cd /wherever/you/downloaded/accs-protocols/
pipx install .
```
however the authors emphasize that people who prefer to use unholy combinations of `conda` and `pip` are valid and equally deserving of love.

## Basic usage
The package provides 3 commands which are briefly described below. **Consult the `--help` option for each command for additional options.**

`npf_init`, run with no arguments, will initialize your local configuration and data directories (see below) to ensure resources such as labware definitions are up to date after installing a different version of `accs-protocols`. By default, the contents of your `protocol_scripts` directory are left alone.

`npf_setup_form <NAME>` , where `<NAME>` is the name of a protocol, launches a browser based setup form for generating a protocol script for that protocol. The `--list` option will print out all valid selections. On completing the form you will be given a path to the unique protocol script file which was generated, which you can then feed to the Opentrons app.

`npf_simulate </path/to/your/script.py>` will simulate your protocol and output a stream of log messages followed by a short report showing predicted tiprack usage as well as final liquid volumes and mixtures in each labware well (look at `run_config['liquid_tracking']['initial_fills']` in an assembled protocol file to see how initial states are defined). The `--test-cond` option allows injecting combinations of variable assignments into the script.

## User directories
By default, `accs-procols` keeps a variety of files in a subdirectory of the current user's home directory called `accs_data`. Of particular interest:

- `accs_data/protocol_scripts`  contains protocol scripts generated using the setup form. These are kept indefinitely for later reference unless you delete them or run `--rm-scripts`.
- `accs_data/labware_defs` contains the custom labware definitions used in ACCS protocols. These should be selected in the settings of the Opentrons app.

## Custom applications
To understand how protocols are defined we suggest to start by examining the `cci_normalization` protocol as an example, since it is the most often used and well maintained.

`CciNormalizationScriptBuilder` (`accs_npf/protocol_script_builder.py`) is responsible for stitching together the relevant library fragments to produce the template for the protocol script files;

`CciNormalizationSetupForm` (`accs_npf/setup_nb/setup_cci_normalization.py`) defines the form fields for the setup form and their relation to `run_config` parameters;

Finally, in the `accs_npf/resources/protocol_parts/` directory:

- `cci_normalization.protocol.py.inc` is where the protocol steps are defined; if you are familiar with Opentrons Python API protocol scripts the structure of the file may look familiar. Notably the function containing the high level protocol steps is named `ps_run()` and it operates on a `CciCellSplitting` context object.
- `cell_splitting_common.py.inc` defines the `CciCellSplitting` class, a subclass of `CellSplitting` , a subclass of `ProtocolSteps`, which wraps the native Opentrons `ProtocolContext`. This class hierarchy is where the various high-level protocol actions are defined (for example, mixing wells on the tilted source plate).
- `cci_normalization.run_config.py.inc` defines the `run_config` mapping which contains an extensive set of configuration parameters that affect various steps of the protocol. During protocol script generation this is used as a template by the `ProtocolScriptBuilder` which mutates some of the configuration values based on setup form entries.
