
# ACCS protocol software

## Contents
This distribution contains the software components of the Automated Cell Culture Splitter, except for the [Cell Counting Imager](https://github.com/czbiohub-sf/accs-cell-counting-imager-pub) software, which is available separately.

Links to the 2024 preprint, CAD models, supplementary documentation and other resources describing the Automated Cell Culture Splitter can be found on the [main repository](https://github.com/czbiohub-sf/2024-accs-pub).

See the `README.md` in `accs_npf/` and in `ot2logbot/` for more information on installation and use.

- `accs-protocols/` contains the tooling for generating protocol script files and the framework for the protocols themselves.
- `ot2logbot/` contains a script which can optionally be installed on the OT-2 to enable Slack alerts for important events.
- `cal_dummy_scripts/` contains a dummy protocol file that can be loaded in the Opentrons app for the purposes of checking labware calibration without loading a full protocol.

## Maintainers
This software is currently maintained by Greg Courville ([:email:](mailto:greg.courville@czbiohub.org)) of the Bioengineering Platform at Chan Zuckerberg Biohub San Francisco.

## License
The software in this repository is published under the BSD 3-Clause License -- see the `LICENSE` file in this directory.

## See also
- [Main ACCS publication repo](https://github.com/czbiohub-sf/2024-accs-pub) (GitHub)
- [Software for the Cell Counting Imager](https://github.com/czbiohub-sf/accs-cell-counting-imager-pub) (GitHub)
