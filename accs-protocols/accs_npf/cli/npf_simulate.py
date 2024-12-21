import argparse
import collections
import logging
import os
from pathlib import Path
import sys

from .. import sanity_checker


def parse_test_cond_arg(s):
    var_name, var_val = s.split("=", 1)
    return var_name.strip(), var_val.strip()


def get_arg_parser(argv):
    parser = argparse.ArgumentParser(
        description="Dry run and analyze a protocol script",
        prog=Path(argv[0]).name
        )
    parser.add_argument(
        "--test-cond",
        help="add a global variable value to test (if you give this argument "
             "multiple times with different VALUEs for each NAME, all "
             "combinations are tested)",
        metavar="NAME=VALUE",
        action="append",
        type=parse_test_cond_arg)
    parser.add_argument(
        "--prepend-line",
        help="prepend LINE to protocol script",
        metavar="LINE",
        action="append")
    parser.add_argument(
        "--prepend-file",
        help="read file from PATH and prepend contents to protocol script",
        metavar="PATH",
        action="append")
    parser.add_argument(
        "--save-runlog",
        metavar="PATH",
        help="save Opentrons runlog text to a file at PATH (for "
             "multiple-condition runs, output filenames will have serial "
             "numbers added)")
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="don't print protocol log output to the screen while simulating, "
             "just show the summary"
        )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="print debug messages"
        )
    parser.add_argument(
        "--no-stop-on-abort",
        action="store_true",
        help="in a multiple-condition test session,"
             "don't stop at the first failure"
        )
    parser.add_argument(
        "proto_path",
        help="path to protocol file")
    return parser


def main():
    parser = get_arg_parser(sys.argv)
    cmdline_args = parser.parse_args(sys.argv[1:])
    logging.basicConfig(
        level=logging.DEBUG if cmdline_args.debug else logging.INFO)

    test_conds = collections.defaultdict(list)
    if cmdline_args.test_cond:
        for k, v in cmdline_args.test_cond:
            test_conds[k].append(v)

    prepend_snips = []
    if cmdline_args.prepend_line:
        for line in cmdline_args.prepend_line:
            prepend_snips.append(line)
    if cmdline_args.prepend_file:
        for path in cmdline_args.prepend_file:
            with open(path, "r") as f:
                prepend_snips.append(f.read())
        prepend_snips.append("")
    prepend_text = "\n\n".join(prepend_snips)

    sanity_checker.main(
        proto_path=cmdline_args.proto_path,
        prepend_text=prepend_text,
        test_conds=test_conds,
        runlog_output_path=cmdline_args.save_runlog,
        quiet=cmdline_args.quiet,
        stop_on_protocol_abort=not cmdline_args.no_stop_on_abort
        )
