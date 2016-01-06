#!/bin/bash
# starting this script on login will wait for Nemo to initialize
# and start xwinwrap with mplayer on desktop surface and play /path/to/your/video

until $(echo xwininfo -name Desktop)|grep "IsViewable"; do :; done
xwinwrap -b -s -fs -fdt -d "Desktop" -- mplayer -wid WID --loop=0 --nosound /path/to/your/video
