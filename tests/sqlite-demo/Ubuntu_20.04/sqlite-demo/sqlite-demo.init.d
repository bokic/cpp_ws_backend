#!/bin/sh

### BEGIN INIT INFO
# Provides:          sqlite-demo
# Required-Start:    $local_fs $remote_fs $network $syslog $named
# Required-Stop:     $local_fs $remote_fs $network $syslog $named
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: starts the sqlite-demo app backend
# Description:       starts sqlite-demo using start-stop-daemon
### END INIT INFO

PATH=/opt/sqlite-demo:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/opt/sqlite-demo/sqlite-demo
NAME=sqlite-demo
DESC=sqlite-demo

test -x $DAEMON || exit 0

. /lib/init/vars.sh
. /lib/lsb/init-functions

start_sqlitedemo() {
        # Start the daemon/service
        #
        # Returns:
        #   0 if daemon has been started
        #   1 if daemon was already running
        #   2 if daemon could not be started
        start-stop-daemon --start --background -c www-data --quiet --exec $DAEMON 2>/dev/null || return 1
}

stop_sqlitedemo() {
        # Stops the daemon/service
        #
        # Return
        #   0 if daemon has been stopped
        #   1 if daemon was already stopped
        #   2 if daemon could not be stopped
        #   other if a failure occurred
        start-stop-daemon --stop --quiet --name $NAME
        RETVAL="$?"
        sleep 1
        return "$RETVAL"
}

case "$1" in
        start)
                log_daemon_msg "Starting $DESC" "$NAME"
                start_sqlitedemo
                case "$?" in
                        0|1) log_end_msg 0 ;;
                        2)   log_end_msg 1 ;;
                esac
                ;;
        stop)
                log_daemon_msg "Stopping $DESC" "$NAME"
                stop_sqlitedemo
                case "$?" in
                        0|1) log_end_msg 0 ;;
                        2)   log_end_msg 1 ;;
                esac
                ;;
        status)
                status_of_proc "$DAEMON" "$NAME" && exit 0 || exit $?
                ;;
esac
