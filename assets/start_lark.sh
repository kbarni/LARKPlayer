#!/bin/sh

LARKEXEC=$([ -f /lib/ld-linux-armhf.so.3 ] && echo "larkplayer" || echo "larkplayer_pw2")
LIBDIR=$([ -f /lib/ld-linux-armhf.so.3 ] && echo "libs_hf/" || echo "libs_pw2/")
#LARKEXEC="larkplayer"

echo "Starting LARK - The Libre Audiobook Player for Kindle..."
lipc-set-prop -s com.lab126.btfd BTenable 0:1
sleep 1
cd /mnt/us/LARK
LD_LIBRARY_PATH=$LIBDIR ./$LARKEXEC

# If the app didn't exit cleanly, it may have left system settings changed
# (screensaver inhibited, BT keepalive enabled). Reset them before killing it.
if pgrep -x $LARKEXEC > /dev/null; then
    lipc-set-prop -s com.lab126.powerd preventScreenSaver 0
    lipc-set-prop -s com.lab126.btfd ensureBTconnection 0
    pkill -x $LARKEXEC
fi
