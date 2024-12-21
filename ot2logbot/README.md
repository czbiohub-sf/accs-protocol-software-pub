
# ot2logbot

## Introduction
`ot2logbot` is a script that runs as a service on the OT-2 and forwards high priority log messages (`INFO`, `WARNING`, `ERROR`) from ACCS protocols to a designated Slack channel.

By default, the bot posts to a channel named after the OT-2's hostname, e.g. `#ot_error_sweetpea`. This can be overridden by assigning a string to `CHANNEL_NAME` in `ot2logbot.py`.

## Get a Slack bot token
Go to https://api.slack.com/apps, click "Create New App", and select "From scratch". Fill out the relevant form fields to set up a new app.

From the "OAuth & Permissions" tab, add the scopes `chat:write` and `chat:write.customize`. Click the button under the "OAuth Tokens" heading to install the app to your workspace.  Once this is done there will be a text field with a token string starting with `xoxb-`. Copy this into `slack_token.txt`.

## Install on the OT-2
Note that having SSH access to the robot is the most convenient way to accomplish these steps but everything can be accomplished with the file manager and terminal in the onboard Jupyter Notebook app.

Create the `ot2logbot` directory on the robot's filesystem:
```
mkdir -p /var/lib/jupyter/notebooks/misc/ot2logbot
```

Copy `ot2logbot.py`, `ot2logbot.service` and `slack_token.txt` to the `ot2logbot` directory. Install the `slack_sdk` package:
```
pip install slack_sdk==3.9.1
```

Now install the unit file to set up `ot2logbot` as a system service:
```
cd /var/lib/jupyter/notebooks/misc/ot2logbot
mount -o remount,rw /
cp ot2logbot.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable ot2logbot
mount -o remount,ro /
```

Try test-running the bot:
```
python ot2logbot.py
```
If everything is correct, you should see a message "Monitoring started" posted to the relevant Slack channel. Press CTRL-C to stop the script.

Now `ot2logbot` should run automatically when the robot is started. To manually start the service:
```
systemctl start ot2logbot
```
