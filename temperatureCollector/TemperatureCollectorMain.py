import argparse
import logging
import sys

# Start like this with these imports
# PYTHONPATH=<root-dir-here>/temperatureCollector python3 temperatureCollector/TemperatureCollectorMain.py

import TemperatureCollectorFlask
import TemperatureCollectorDB

class TemperatureCollectorMain:
    def __init__(self):
        self.logger = logging.getLogger("TemperatureCollector.Main")
        self.logger.setLevel(logging.INFO)
        handler = logging.StreamHandler(sys.stdout)
        handler.setLevel(logging.INFO)
        self.logger.addHandler(handler)


    #
    # Parse command-line args and set them in a CollectorConfig object
    #
    def parse_config(self, args):
        config = TemperatureCollectorFlask.CollectorConfig()
        parser = argparse.ArgumentParser(description='Temperature Collector Server')
        parser.add_argument('-i', '--listen-ip', type=str,
                            help=f'Local IP address to listen to, default: {config.host}')
        parser.add_argument('-p', '--listen-port', type=str,
                            help=f'Local TCP port to listen to, default: {str(config.port)}')
        parser.add_argument('-d', '--db-path', type=str,
                            help=f'Filesystem path to database file: {config.db_path}')

        args_out = parser.parse_args(args)

        # Now store the options that have been set
        if args_out.listen_ip:
            config.host = args_out.listen_ip

        if args_out.listen_port:
            config.port = args_out.listen_port

        if args_out.db_path:
            config.db_path = args_out.db_path

        return config

    def main(self):
        collector_config = self.parse_config(sys.argv[1:])
        if not collector_config:
            return

        # Instantiate the DB
        db = TemperatureCollectorDB.TemperatureCollectorDB(collector_config.db_path)
        if not db.connect():
            self.logger.error("Error connecting to DB, exiting")
            return

        # Start the HTTP REST server
        self.logger.info("Starting Temperature Collector Flask server at " \
                        f"{collector_config.host} {collector_config.port}")
        collector_server = TemperatureCollectorFlask.Server(collector_config, db)
        collector_server.run()

#
# No need to execute this script with flask, instead execute as follows:
#     python3 ???.py [CLI args]
#
if __name__ == '__main__':
    TemperatureCollectorMain().main()
