#!/bin/env python

import os
from pathlib import Path
import select
import socket
import sys
import time

import pytz
from slack_sdk.web import WebClient
import systemd.journal


MIN_POST_INTERVAL_S = 5.
MIN_PING_INTERVAL_S = 300.
TIMEZONE_STR = "America/Los_Angeles"
TOKEN_FILE_PATH = Path(__file__).parent / "slack_token.txt"
CHANNEL_NAME = None


def post_hello_message(client, channel_name, bot_name):
    client.chat_postMessage(
        channel=channel_name,
        text="Monitoring started.",
        username=bot_name,
        # icon_emoji=":opentrons_oops:"
        icon_emoji=":robot_face:"
    )


def post_messages(client, channel_name, bot_name, messages, ping=True):
    client.chat_postMessage(
        channel=channel_name,
        text=format_messages(messages, ping=ping),
        username=bot_name,
        # icon_emoji=":opentrons_oops:"
        icon_emoji=":robot_face:"
    )


def enquote_message(text):
    return "```" + text.replace("`", "`\u2060") + "```"


def format_priority(priority):
    if priority <= systemd.journal.LOG_EMERG:
        return "EMERGENCY"
    elif priority <= systemd.journal.LOG_ALERT:
        return "ALERT"
    elif priority <= systemd.journal.LOG_CRIT:
        return "CRITICAL"
    elif priority <= systemd.journal.LOG_ERR:
        return "ERROR"
    elif priority <= systemd.journal.LOG_WARNING:
        return "WARNING"
    elif priority <= systemd.journal.LOG_NOTICE:
        return "NOTICE"
    elif priority <= systemd.journal.LOG_INFO:
        return "INFO"
    else:
        return "DEBUG"


def format_messages(messages, ping=True):
    tz = pytz.timezone(TIMEZONE_STR)
    return "\n".join(
        f"*{format_priority(priority)}* at "
        f"{str(ts.astimezone(tz))}: {'<!here>' if ping and not i else ''}\n"
        + enquote_message(text)
        for i, (priority, ts, text) in enumerate(messages)
        )


if __name__ == "__main__":
    bot_name = socket.gethostname()
    channel_name = CHANNEL_NAME
    if channel_name is None:
        channel_name = "#ot2_error_" + bot_name.lower().rsplit("ot2-", 1)[-1]

    slack_token = Path(TOKEN_FILE_PATH).read_text().strip()
    client = WebClient(token=slack_token)
    post_hello_message(client, channel_name, bot_name)

    reader = systemd.journal.Reader()
    reader.log_level(systemd.journal.LOG_NOTICE)
    reader.add_match(_SYSTEMD_UNIT="opentrons-robot-server.service")
    reader.seek_tail()
    reader.get_previous()
    poll = select.poll()
    poll.register(reader, reader.get_events())

    last_post_time = None
    last_ping_time = None
    lines = []
    while True:
        if poll.poll(1000):
            if reader.process() == systemd.journal.APPEND:
                for entry in reader:
                    if "LOGGER" not in entry or entry["LOGGER"] not in (
                            "opentrons.api.session",
                            "opentrons.user_protocol"
                            ):
                        continue
                    lines.append((
                        entry["PRIORITY"],
                        entry["__REALTIME_TIMESTAMP"],
                        entry["MESSAGE"]))

        now = time.monotonic()
        post = last_post_time is None \
            or now - last_post_time > MIN_POST_INTERVAL_S
        if lines and post:
            ping = last_ping_time is None \
                or now - last_ping_time > MIN_PING_INTERVAL_S
            post_messages(
                client, channel_name, bot_name, lines, ping=ping)
            lines = []
            last_post_time = now
            if ping:
                last_ping_time = now
