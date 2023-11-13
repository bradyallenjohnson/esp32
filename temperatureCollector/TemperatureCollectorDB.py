import logging
import sqlite3
import os
import sys
import traceback

# Table Creations
QUERY_STR_CREATE_DEV_INFO_TABLE = """
    CREATE TABLE IF NOT EXISTS device_info (
          device_id TEXT PRIMARY KEY,
          device_ip TEXT NOT NULL,
          location TEXT);
"""
QUERY_STR_CREATE_DEV_TEMP_TABLE = """
    CREATE TABLE IF NOT EXISTS device_temperature (
        device_id REFERENCES device_info(device_id),
        temperature TEXT NOT NULL,
        humidity TEXT NOT NULL,
        epoch_time INTEGER NOT NULL);
"""

# device_info table queries
QUERY_STR_INSERT_DEV_INFO = """
    INSERT INTO device_info (device_id, device_ip)
    VALUES (:device_id, :device_ip);
"""
QUERY_STR_UPDATE_DEV_INFO = """
    UPDATE device_info SET device_ip=:device_ip
    WHERE device_id=:device_id;
"""
QUERY_STR_UPDATE_DEV_LOCATION = """
    UPDATE device_info SET location=:location
    WHERE device_id=:device_id;
"""
QUERY_STR_SELECT_DEV_INFO = """
    SELECT * FROM device_info WHERE device_id=:device_id;
"""
QUERY_STR_SELECT_ALL_DEV_INFO = """
    SELECT * FROM device_info;
"""
QUERY_STR_DELETE_DEV_INFO = """
    DELETE FROM device_info where device_id = :device_id;
"""

# device_temperature table queries
QUERY_STR_INSERT_DEV_TEMP = """
    INSERT INTO device_temperature (device_id, temperature, humidity, epoch_time)
    VALUES (:device_id, :temperature, :humidity, :epoch_time);
"""
QUERY_STR_SELECT_DEV_TEMP = """
    SELECT * FROM device_temperature where device_id = :device_id;
"""
QUERY_STR_SELECT_ALL_DEV_TEMP = """
    SELECT * FROM device_temperature where device_id = :device_id;
"""
QUERY_STR_DELETE_DEV_TEMP = """
    DELETE FROM device_temperature where device_id = :device_id;
"""

class TemperatureCollectorDB:
    def __init__(self, db_path):
        self.logger = logging.getLogger("TemperatureCollector.DB")
        self.logger.setLevel(logging.INFO)
        handler = logging.StreamHandler(sys.stdout)
        handler.setLevel(logging.INFO)
        self.logger.addHandler(handler)

        self.db_path = db_path
        self.connection = None

    def dict_factory(self, cursor, row):
        fields = [column[0] for column in cursor.description]
        return {key: value for key, value in zip(fields, row)}


    def connect(self):
        try:
            # Check if we're already connected
            if self.connection:
                self.logger.info("Already connected to the DB.")
                return True

            # Check if its an existing or a new DB
            if os.path.exists(self.db_path):
                self.logger.info("Opening an existing DB")
            else:
                self.logger.info("Creating a new DB")
                
            self.connection = sqlite3.connect(
                database=self.db_path,
                detect_types=sqlite3.PARSE_DECLTYPES,
                check_same_thread=False)
            self.connection.row_factory = self.dict_factory
            self.logger.info(f"Connection to SQLite DB successful: {self.db_path}")
        except sqlite3.Error as e:
            self.logger.error(f"SQLite DB connection error: '{e}'")
            return False

        # Create the database tables
        if not self.__execute_write_query(QUERY_STR_CREATE_DEV_INFO_TABLE):
            return False
        if not self.__execute_write_query(QUERY_STR_CREATE_DEV_TEMP_TABLE):
            return False

        return True

    #
    # device_info API
    #
    def insert_device_info(self, device_id, device_ip):
        query_dict = {"device_id": device_id,
                      "device_ip": device_ip}

        return self.__execute_write_query(QUERY_STR_INSERT_DEV_INFO, query_dict)

    def update_device_info(self, device_id, device_ip):
        query_dict = {"device_id": device_id,
                      "device_ip": device_ip}

        return self.__execute_write_query(QUERY_STR_UPDATE_DEV_INFO, query_dict)

    def update_device_info_location(self, device_id, location_str):
        query_dict = {"device_id": device_id,
                      "location":  location_str}

        return self.__execute_write_query(QUERY_STR_UPDATE_DEV_LOCATION, query_dict)

    def get_all_device_info(self):
        return self.__execute_read_query(QUERY_STR_SELECT_ALL_DEV_INFO)

    def get_device_info(self, device_id):
        query_dict = {"device_id": device_id}

        result = self.__execute_read_query(QUERY_STR_SELECT_DEV_INFO, query_dict)
        #self.logger.info(f"get_device_temperature() num entries: {len(result)}")

        return result

    def delete_device_info(self, device_id):
        query_dict = {"device_id": device_id}

        return self.__execute_write_query(QUERY_STR_DELETE_DEV_INFO, query_dict)

    #
    # device_temperature API
    #
    def insert_device_temperature(self, device_id, temperature, humidity, epoch_time):
        query_dict = {"device_id":   device_id,
                      "temperature": temperature,
                      "humidity":    humidity,
                      "epoch_time":  epoch_time}

        return self.__execute_write_query(QUERY_STR_INSERT_DEV_TEMP, query_dict)

    def get_all_device_temperature(self):
        return self.__execute_read_query(QUERY_STR_SELECT_ALL_DEV_TEMP)

    def get_device_temperature(self, device_id):
        query_dict = {"device_id":   device_id}

        result = self.__execute_read_query(QUERY_STR_SELECT_DEV_TEMP, query_dict)
        #self.logger.info(f"get_device_temperature() num entries: {len(result)}")

        return result

    def delete_device_temperature(self, device_id):
        query_dict = {"device_id":   device_id}

        return self.__execute_write_query(QUERY_STR_DELETE_DEV_TEMP, query_dict)

    #
    # Low level DB util functions
    #

    # Internal function to execute a DB write query
    def __execute_write_query(self, query_str, query_dict=None):
        if not self.connection:
            self.logger.error("Not connected to DB yet, not possible to execute write query")
            return False

        cursor = self.connection.cursor()
        try:
            if query_dict is None:
                cursor.execute(query_str)
            else:
                cursor.execute(query_str, query_dict)
            self.connection.commit()
            self.logger.debug("Write Query executed successfully")
        except sqlite3.Error as e:
            self.logger.error(f"Error executing write query: {query_str}")
            self.logger.error(traceback.format_exc())
            self.logger.error(''.join(traceback.format_stack()))
            return False

        return True

    # Internal function to execute a DB write query
    def __execute_read_query(self, query_str, query_dict=None):
        if not self.connection:
            self.logger.error("Not connected to DB yet, not possible to execute read query")
            return False

        cursor = self.connection.cursor()
        try:
            if query_dict is None:
                cursor.execute(query_str)
            else:
                cursor.execute(query_str, query_dict)
            return cursor.fetchall()
        except sqlite3.Error as e:
            self.logger.error(f"Error executing read query: {query_str}")
            self.logger.error(traceback.format_exc())
            self.logger.error(''.join(traceback.format_stack()))
            return None
