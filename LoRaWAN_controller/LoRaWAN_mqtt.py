import json
import time
from base64 import b64encode, b64decode
import paho.mqtt.client as mqtt
from queue import Queue

# Insert username and password for TTN here 
USER = ''
PASS = ''


class LoRaWANClient(object):
    COMMANDS = {
        'reconf_sink': '00',
        'chg_interval': '01',
        'nop': 'FF',
    }
    
    def __init__(self):
        self.wait_time = 10
        self.consensus_percentage = 0.5
        self.available_sinks = ['0013a20041f223b8', '0013A20041F223B2']
        self.current_sink = 0

        self.devices = {}
        self.new_start_time = None
        
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.username_pw_set(USER, PASS)
        self.client.tls_set()

        self.client.connect("eu1.cloud.thethings.network", 8883, 60)
        
    def update_sink_node(self, data):
        num_of_devices_reporting_fail = sum(1 for d in data if d > 0)
        sink_failed = num_of_devices_reporting_fail > self.consensus_percentage * len(data)
        
        if sink_failed:
            self.current_sink = (self.current_sink + 1) % len(self.available_sinks)
            print(f'Sink failed! New sink: {self.current_sink} : {self.available_sinks[self.current_sink]}')
            return self.COMMANDS['reconf_sink'] + self.available_sinks[self.current_sink]
                
        return self.COMMANDS['nop']

    def run(self):
        while True:
            # TODO: This assumes that messages either arrive close to each other or not at all.
            # If the set time has passed after the first message, take values 
            # from all devices that have recently sent a message.
            if self.new_start_time is not None and time.time() > self.new_start_time + self.wait_time:
                values = []
                for topic, data in self.devices.items():
                    if data['timestamp'] >= self.new_start_time - 1:
                        values.append(data['value'])
                print(f'Wotking with information from {len(values)}/{len(self.devices)} devices: {values}.')
                
                down_value = self.update_sink_node(values)
                
                value = LoRaWANClient.prepare_tx_frame(down_value)
                print(f'Publishing {value}')
                for topic in self.devices:   
                    self.client.publish(f"{topic}/down/replace", value)
                self.new_start_time = None
            
            time.sleep(0.1)
            self.client.loop()

    # The callback for when the client receives a CONNACK response from the server.
    def on_connect(self, client, userdata, flags, rc):
        print(f"Connected with result code {rc}.")

        # Subscribing in on_connect() means that if we lose the connection and
        # reconnect then subscriptions will be renewed.
        self.client.subscribe("#", 0)

    # The callback for when a PUBLISH message is received from the server.
    def on_message(self, client, userdata, msg):
        topic_components = msg.topic.split('/')
        topic_prefix = '/'.join(topic_components[:4])
        topic_suffix = '/'.join(topic_components[4:])
        
        data = json.loads(msg.payload)
        # print(json.dumps(data, indent=2))
        
        if topic_suffix == 'up':
            # Read data
            device, timestamp, value = LoRaWANClient.decode_rx_frame(data)
            print(f'Received @{device}: {value}')
            
            self.devices[topic_prefix] = {
                'device': device, 
                'timestamp': LoRaWANClient.convert_timestamp(timestamp),
                'value': value
            }
            if self.new_start_time is None:
                self.new_start_time = LoRaWANClient.convert_timestamp(timestamp)
            
    @staticmethod
    def prepare_tx_frame(payload):
        value = {
            "downlinks": [{
                "f_port": 15,  # 1-233 allowed
                "frm_payload": b64encode(bytes.fromhex(payload)).decode(),
                "priority": "NORMAL"
            }]
            }
            
        value = json.dumps(value)
        return value
    
    @staticmethod
    def decode_rx_frame(data):
        payload = data['uplink_message']['frm_payload']
        value = int.from_bytes(b64decode(payload), 'little')
        device = data['end_device_ids']['device_id']
        timestamp = data['received_at']
        
        return device, timestamp, value
    
    @staticmethod
    def convert_timestamp(timestamp):
        return time.mktime(time.strptime(timestamp.split('.')[0], '%Y-%m-%dT%H:%M:%S')) + 2 * 60 * 60
    
            
            
def main():
    client = LoRaWANClient()
    try:
        client.run()
    except KeyboardInterrupt:
        pass

if __name__ == '__main__':
    main()