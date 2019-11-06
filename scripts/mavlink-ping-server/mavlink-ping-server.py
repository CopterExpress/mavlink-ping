#!/usr/bin/env python2.7
# -*- coding: utf-8 -*-

import argparse
import os

from pymavlink import mavutil

MAVLINK20_ENV = 'MAVLINK20'  # MAVLink v2.0 environment variable name
APP_NAME = 'MAVLink ping server v0.1'

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=APP_NAME)
    parser.add_argument("-d", "--debug", action="store_true",
                        help="enable debug output")
    parser.add_argument('url')

    args = parser.parse_args()

    del parser

    print(APP_NAME)
    if args.debug:
        print('')

    env_cleanup = False  # Environment cleanup flag
    if MAVLINK20_ENV not in os.environ:  # Check if MAVLINK20 is set globally
        # Force pymavlink to use MAVLink v2.0 using environment variable
        os.environ[MAVLINK20_ENV] = ''
        if args.debug:
            print('{0} environment variable is set'.format(MAVLINK20_ENV))

        # It's a good idea to clean this flag for other programs
        env_cleanup = True

    # Forcing pymavlink to use common MAVLink dialect
    # HINT: The result depends on the MAVLINK20 environment variable
    mavutil.set_dialect('common')

    if env_cleanup:  # Environment cleanup needed
        del os.environ[MAVLINK20_ENV]  # No need in this environmental variable further
        if args.debug:
            print('{0} environment variable is removed'.format(MAVLINK20_ENV))

    # Importing mavlink module from pymavlink
    from pymavlink.mavutil import mavlink

    if args.debug:
        print('MAVLink module loaded: {0}'.format(mavlink))

    mavlink_con = mavutil.mavlink_connection(args.url, source_system=255,
                                             source_component=mavlink.MAV_COMP_ID_AUTOPILOT1)

    try:
        while True:
            msg = mavlink_con.recv_match(type='PING', condition='not PING.target_system and not PING.target_component',
                                         blocking=True)
            if args.debug:
                print('Incoming ping request: {0}'.format(msg))
            mavlink_con.mav.ping_send(0, msg.seq, msg.get_header().srcSystem, msg.get_header().srcComponent)
    except KeyboardInterrupt:
        pass
