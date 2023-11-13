import logging
import json
import time
from flask import Flask, make_response, request

#
# Flask Global Variables
# More about storing data in the AppContext here:
#     https://flask.palletsprojects.com/en/2.0.x/appcontext/#storing-data
#

class CollectorConfig:
    def __init__(self):
        self.port = 8182
        self.host = '0.0.0.0'
        self.db_path = './collector.db'
        self.KEY_ZONE_TEMP   = 'zone-temperature'
        self.KEY_DEVICE_REG  = 'device-registration'
        self.KEY_DEVICE_MAC  = 'device'
        self.KEY_DEVICE_IP   = 'device-ip'
        self.KEY_LOCATION    = 'location'
        self.KEY_TEMPERATURE = 'temperature'
        self.KEY_HUMIDITY    = 'humidity'

class request_context:
    def __init__(self, device_mac=None, device_ip=None, device_mac_nocolon=None, request=None):
        self.device_mac = device_mac
        self.device_mac_nocolon = device_mac_nocolon
        self.device_ip = device_ip
        self.request = request

class Server:
    def __init__(self, collector_config, collector_db):
        self.collector_config = collector_config
        self.collector_db = collector_db
        self.app = Flask(__name__)
        self.logger = self.app.logger
        self.logger.setLevel(logging.INFO)

        self.app.add_url_rule('/bj/api/v1.0/temperature/<device_mac>',
                              view_func=self.temperature_flask_view,
                              methods=['GET', 'POST', 'DELETE'])
        self.app.add_url_rule('/bj/api/v1.0/device/<device_mac>',
                              view_func=self.device_flask_view,
                              methods=['GET', 'POST', 'DELETE'])
        self.app.add_url_rule('/bj/api/v1.0/device',
                              view_func=self.devices_flask_view,
                              methods=['GET'])

    def get_flask_app(self):
        return self.app

    def get_collector_config(self):
        return self.collector_config

    #
    # Flash view functions
    #

    #
    # Flask view function to handle the Device registration HTTP GET message to
    # get all registered devices.
    #
    # Can be invoked as follows:
    #
    # curl -X GET http://127.0.0.1:8182/bj/api/v1.0/device -H "Content-Type: application/json"
    #
    def devices_flask_view(self):
        if request.method != 'GET':
            return make_response("Unsupported operation", 405)

        device_entries = self.collector_db.get_all_device_info()
        if device_entries is None:
            return make_response("GET devices: Internal Database error retrieving device", 500)
        else:
            return make_response(json.dumps(device_entries), 200)

    #
    # Flask view function to handle the Device registration HTTP Post message
    # Can be invoked as follows:
    #
    # curl -X POST http://127.0.0.1:8182/bj/api/v1.0/device/20%3a1e%3a88%3a23%3a90%3a88 \
    #      -H "Content-Type: application/json" \
    #      -d '{"device-registration" : {"device"    : "20%3a1e%3a88%3a23%3a90%3a88",  \
    #                                    "device-ip" : "192.168.100.16"}}'
    #                                    "location"  : "garage"}}'
    #
    # curl -X GET http://127.0.0.1:8182/bj/api/v1.0/device/20%3a1e%3a88%3a23%3a90%3a88 \
    #      -H "Content-Type: application/json"
    #
    # Notice the colons in the mac are escaped with %3a resulting in 20:1e:88:23:90:88 
    # Entries will be inserted in the DB without colons
    #
    # The route decorator tells Flask what URL should trigger this function
    # To avoid having a global app variable, using add_url_rule() instead of app.route() decorator.
    #
    #@app.route('/bj/api/v1.0/register/<mac>', methods=['GET', 'POST'])
    def device_flask_view(self, device_mac):
        context = request_context(device_mac=device_mac,
                                  device_mac_nocolon=device_mac.replace(':', ''),
                                  request=request)

        if request.method == 'POST':
            return self.device_flask_post(context)
        elif request.method == 'GET':
            return self.device_flask_get(context)
        elif request.method == 'DELETE':
            return self.device_flask_delete(context)

        return make_response("Unsupported operation", 405)

    #
    # HTTP GET handler, called from device_flask_view()
    #
    def device_flask_get(self, context):
        device_entry = self.collector_db.get_device_info(context.device_mac_nocolon)
        if device_entry is None:
            return make_response("GET: Internal Database error retrieving device", 500)

        self.logger.debug(f"device {device_entry}");
        if len(device_entry) <= 0:
            return make_response(f"Device does not exist {context.device_mac_nocolon}", 500)
        else:
            self.logger.info(device_entry)
            return make_response(json.dumps(device_entry), 200)

    #
    # HTTP DELETE handler, called from device_flask_view()
    #
    def device_flask_delete(self, context):
        if not self.collector_db.delete_device_temperature(context.device_mac_nocolon):
            return make_response("DELETE Temperature: Internal Database error deleting device", 500)

        if not self.collector_db.delete_device_info(context.device_mac_nocolon):
            return make_response("DELETE Device: Internal Database error retrieving device", 500)
        else:
            return make_response("Device deleted", 201)
    #
    # HTTP POST handler, called from device_flask_view()
    #
    def device_flask_post(self, context):
        request_data = context.request.get_json()
        if not request_data:
            return make_response("Bad request: Did not receive any JSON data", 400)

        # Validate the received JSON
        request_dict = request_data.get(self.collector_config.KEY_DEVICE_REG)
        if not request_dict:
            return make_response("Bad request: Did not receive the root Zone Temperature JSON data", 400)

        device_id = request_dict.get(self.collector_config.KEY_DEVICE_MAC)
        device_ip = request_dict.get(self.collector_config.KEY_DEVICE_IP)
        for key, val in [(self.collector_config.KEY_DEVICE_MAC, device_id),
                         (self.collector_config.KEY_DEVICE_IP,  device_ip)]:
            if not val:
                return make_response(f"Bad request: Did not receive {key}", 400)

        # Get the device to see if we need to update it or insert it
        # The device IP may change
        device_entry = self.collector_db.get_device_info(context.device_mac_nocolon)
        if device_entry is None:
            return make_response("Internal Database error retrieving device", 500)
            
        if len(device_entry) <= 0:
            result = self.collector_db.insert_device_info(context.device_mac_nocolon, device_ip)
        else:
            result = self.collector_db.update_device_info(context.device_mac_nocolon, device_ip)

        if not result:
            return make_response("Internal Database error inserting device", 500)
        
        # The device location is optional
        location_str = request_dict.get(self.collector_config.KEY_LOCATION)
        self.logger.debug(f"location_str {location_str}")
        if location_str:
            result = self.collector_db.update_device_info_location(context.device_mac_nocolon, location_str)
            if not result:
                return make_response("Internal Database error updating device location", 500)
 
        return make_response('OK', 201)

    #
    # Flask view function to handle the Device Temperature HTTP Post message
    # Can be invoked as follows:
    #
    # curl -X POST http://127.0.0.1:8182/bj/api/v1.0/temperature/20%3a1e%3a88%3a23%3a90%3a88 \
    #      -H "Content-Type: application/json" \
    #      -d '{"zone-temperature" : {"device"      : "20%3a1e%3a88%3a23%3a90%3a88", \
    #                                 "temperature" : "degrees-celsius", \
    #                                 "humidity"    : "humidity-percentage"}}'
    #
    # curl -X GET http://127.0.0.1:8182/bj/api/v1.0/temperature/20%3a1e%3a88%3a23%3a90%3a88 \
    #      -H "Content-Type: application/json"
    #
    # Notice the colons in the mac are escaped with %3a resulting in 20:1e:88:23:90:88 
    # Entries will be inserted in the DB without colons
    #
    # The route decorator tells Flask what URL should trigger this function
    # To avoid having a global app variable, using add_url_rule() instead of app.route() decorator.
    #
    #@app.route('/bj/api/v1.0/temperature/<mac>', methods=['GET', 'POST'])
    def temperature_flask_view(self, device_mac):
        context = request_context(device_mac=device_mac,
                                  device_mac_nocolon=device_mac.replace(':', ''),
                                  request=request)

        if request.method == 'POST':
            return self.temperature_flask_post(context)
        elif request.method == 'GET':
            return self.temperature_flask_get(context)
        elif request.method == 'DELETE':
            return self.temperature_flask_delete(context)

        return make_response("Unsupported operation", 405)

    #
    # HTTP GET handler, called from temperature_flask_view()
    #
    def temperature_flask_get(self, context):
        if context.device_mac_nocolon is None:
            self.logger.error("temperature_flask_get no device_mac")
            return make_response(f"Bad request: Did not receive {self.collector_config.KEY_DEVICE_MAC}", 400)

        result = self.collector_db.get_device_temperature(context.device_mac_nocolon)
        self.logger.debug(f"device {context.device_mac_nocolon} has {len(result)} entries")
        if result is None:
            return make_response(f"Device does not exist {context.device_mac_nocolon}", 500)
        else:
            #return make_response(f"Device Temperature entries {len(result)}", 200)
            return make_response(json.dumps(result), 200)

    #
    # HTTP DELETE handler, called from device_flask_view()
    #
    def temperature_flask_delete(self, context):
        if not self.collector_db.delete_device_temperature(context.device_mac_nocolon):
            return make_response("DELETE Temperature: Internal Database error deleting device", 500)
        else:
            return make_response("Device Temperatures deleted", 201)

    #
    # HTTP POST handler, called from temperature_flask_view()
    #
    def temperature_flask_post(self, context):
        request_data = context.request.get_json()
        if not request_data:
            return make_response("Bad request: Did not receive any JSON data", 400)

        # Validate the received JSON
        request_dict = request_data.get(self.collector_config.KEY_ZONE_TEMP)
        if not request_dict:
            return make_response("Bad request: Did not receive the root Zone Temperature JSON data", 400)

        device_id   = request_dict.get(self.collector_config.KEY_DEVICE_MAC)
        temp        = request_dict.get(self.collector_config.KEY_TEMPERATURE)
        humidity    = request_dict.get(self.collector_config.KEY_HUMIDITY)

        for key, val in [(self.collector_config.KEY_DEVICE_MAC, device_id),
                         (self.collector_config.KEY_TEMPERATURE, temp),
                         (self.collector_config.KEY_HUMIDITY, humidity)]:
            if not val:
                return make_response(f"Bad request: Did not receive {key}", 400)

        # time.time() returns a float with the number of seconds since the epoch
        if not self.collector_db.insert_device_temperature(
            context.device_mac_nocolon, temp, humidity, int(time.time())):
            return make_response("Internal Database error", 500)
        else:
            return make_response('OK', 201)

    #
    # Run the Flask server
    #
    def run(self):
        #self.app.run(host=self.collector_config.host, port=self.collector_config.port, debug=True)
        self.app.run(host=self.collector_config.host, port=self.collector_config.port)
