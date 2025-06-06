% portsentry.conf(8) | System Manager's Manual

# NAME

**portsentry.conf** \- Configuration file for Portsentry

# DESCRIPTION

The portsentry.conf file is used to configure Portsentry. The default location for this file is **/etc/portsentry/portsentry.conf**. The configuration file is read by Portsentry at startup. The configuration file is a simple text file with one option per line. Lines starting with a **#** are considered comments and are ignored.

There is an example configuration file included with portsentry and available at [Github](https://github.com/portsentry/portsentry/blob/master/examples/portsentry.conf) with additional comments, examples and recommendations for how to setup Portsentry.

# OPTIONS

## TCP_PORTS="\<ports\>"

A comma separated list of TCP ports you want to monitor. You can specify inclusive port ranges with a dash, e.g, 10-20 to include port 10 to 20.

### Example

TCP_PORTS="10,20,30-40"

## UDP_PORTS="\<ports\>"

A comma separated list of UDP ports you want to monitor. You can specify inclusive port ranges with a dash, e.g, 10-20 to include port 10 to 20.

### Example

UDP_PORTS="10,20,30-40"

## IGNORE_FILE="\<path to ignore file\>"

Path to a file with IP addresses to ignore when logging and taking actions. Any IP/CIDR address in this file will be ignored. This is useful for ignoring local traffic or other trusted hosts.

## HISTORY_FILE="\<path to history file\>"

This file contains all the IP addresses that have triggered the Portsentry detection engine in some way. See the [HOWTO-Logfile](https://github.com/portsentry/portsentry/blob/master/docs/HOWTO-Logfile.md) for more information on how to read this file.

## BLOCKED_FILE="/tmp/portsentry.blocked"

When Portsentry's action mechanism is used, this file will contain a list of all hosts that triggers an action. If a host is matched against this file no further action will be taken.


## RESOLVE_HOST = "<1|0>"

DNS Name resolution - Setting this to "1" will turn on DNS lookups for attacking hosts. Setting it to "0" (or any other value) will not perform DNS lookups. Default is "0".

NOTE: Using DNS resolution can slow down the response time of Portsentry


## BLOCK_TCP="0|1|2" and BLOCK_UDP="0|1|2"

This option controls how Portsentry reacts to a detected connection attempt.

0 = Do not block UDP/TCP scans. The detected connection attempt is only logged. This could be useful for monitoring purposes or use of an external tool, such as fail2ban. This option is the default.

NOTE: It is highly recommended to only log connection attempts (by using: BLOCK_TCP="0" and BLOCK_UDP="0") and use an external tool such as fail2ban to block the attacking host based on the log file generated by Portsentry. The reason for this is that other tools are more sophisticated. For example, fail2ban will persist the blocked host across reboots.

1 = Block UDP/TCP scans. This option will block the attacking host after the scan is detected using the technique specified in the KILL_ROUTE and/or KILL_HOSTS_DENY section below. If KILL_ROUTE is defined, it will run first, followed by KILL_HOSTS_DENY if it is set. If the KILL_RUN_CMD option is set, the command will also be executed.

NOTE: These options are preserved as a legacy option for those who cannot use an external tool to block the attacking host or has some specific use-case, where this method is preferred.

2 = Run external command only (KILL_RUN_CMD). This option will only run the external command specified in the KILL_RUN_CMD

## KILL_ROUTE="<shell command>"

The KILL_ROUTE option is used to drop blacklist the attacking host. This can be done in a number of ways depending on your OS.  The string $TARGET$ is replaced with the attacking host.

### Example

KILL_ROUTE="/usr/local/bin/iptables -I INPUT -s $TARGET$ -j DROP"

## KILL_HOSTS_DENY="<hosts deny entry>"

This text will be dropped into the hosts.deny file for wrappers to use.

### Example

KILL_HOSTS_DENY="ALL: $TARGET$"

## KILL_RUN_CMD="/some/path/here/script $TARGET$ $PORT$"

This is a command that can be run when Portsentry is triggered, it can be whatever you want it to be.

## KILL_RUN_CMD_FIRST = "0|1"

The KILL_RUN_CMD_FIRST value should be set to "1" to force the command to run *before* the KILL_ROUTE/KILL_HOSTS_DENY is executed and should be set to "0" to make the command run *after* the blocking has occurred.

## SCAN_TRIGGER="0"

Enter the number of port connects from the same host you will allow before an alarm is given. The default is 0, which will react immediately.

## PORT_BANNER="*** UNAUTHORIZED ACCESS PROHIBITED *** YOUR CONNECTION ATTEMPT HAS BEEN LOGGED."

When Portsentry is used in "connect" mode [Legacy option] you can specify a banner to be displayed to the connecting host. Once the banner is displayed, the connection will be closed.

## EXAMPLES

View the example configuration file [portsentry.conf](https://github.com/portsentry/portsentry/blob/master/examples/portsentry.conf)

## FILES

/etc/portsentry/portsentry.conf

## BUGS

All bugs should be reported via the portsentry github issue tracker https://github.com/portsentry/portsentry/issues

## AUTHORS

Marcus Hufvudsson <mh@protohuf.com>

## SEE ALSO

portsentry(8)

## LICENSE

Portsentry is licensed under the Common Public License v1.0
