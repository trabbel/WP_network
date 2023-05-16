import time
import serial
import logging
import argparse
import datetime
import threading
from pathlib import Path
from collections import Counter

from zigbee import writeFrame, readFrame
from data2csv import data2csv


class DataSink(object):
    def __init__(self, file_path=None):
        if file_path is None:
            file_path = file_path = Path(__file__).resolve().parents[1] / 'measurements/ZigBee/'
        self.file_path = Path(file_path)
        
        self.csv_objects = {}
        self.statistics = Counter()
        self.connected_devices =[]
        self.default_string = 'test~'
        
        self.tx_buf = bytearray(300)
        self.rx_buf = bytearray(300)
        
        self.xbee = serial.Serial('/dev/ttyS1', 115200)
        
        self.is_running = True
        self.thread = threading.Thread(target=self.receive_loop)
        
    def start(self):
        self.thread.start()
        
        while True:
            # self.send_string()
            print(self.statistics)
            time.sleep(5)
        
    def stop(self):
        self.is_running = False
        
    def send_string(self, string=None):
        if string is None:
            string = self.default_string
        payload = bytearray(string.encode())
        length = writeFrame(self.tx_buff, 0xFFFE, 0x0013a20041f223b8, payload, len(payload))
        print(f'Sending\n\t{payload=}\n\ttx_buff={self.tx_buff[:length]}\n\t{length=}')
        
    def receive_loop(self):
        while self.is_running:
            if self.xbee.in_waiting > 0:
                self.rx_callback()
                
    def rx_callback(self):
        length = 0
        c = b''
        timeoutCnt = 0
        # Wait until a frame delimiter arrives
        while self.xbee.in_waiting > 0 and c != b'\x7E':
            c = self.xbee.read()

        if c != b'\x7E':
            return

        # Parse the frame. In case of an error, a negative length is returned.
        result = readFrame(self.xbee, self.rx_buf)
        length = result[1]#.length

        # print(f"Payload Size: {length}")

        if length <= 0:
            return

        # for i in range(length):
        #     print(f"{self.rx_buf[i]:02x}", end='')
        # print()
        
        # Packet received frame
        if result[0] == 0x90:
            # get name
            mac = str(self.rx_buf[1:9].hex())
            self.statistics[mac[-4:]] += 1
            # check if directory exist
            if mac not in self.connected_devices:
                self.connected_devices.append(mac)
                self.ensure_directory(mac)
                file_name = f"{datetime.datetime.now().strftime('%Y_%m_%d-%H_%M_%S')}.csv"
                self.csv_objects[mac] = data2csv(self.file_path / mac, file_name)
            # save first byte
            e = self.csv_objects[mac].write2csv(chr(self.rx_buf[12]))
            if e is not None:
                logging.error("Writing to csv file failed with error:\n%s\n\n\
                    Continuing because this is not a fatal error.", e)
            #print(f"{self.rx_buf[12:length].decode('utf-8')}")
            
    def ensure_directory(self, deviceName):
        directory = self.file_path / deviceName
        directory.mkdir(parents=True, exist_ok=True) 


def main():
    parser = argparse.ArgumentParser(description="Arguments for the sensor node.")
    parser.add_argument('--dir', action='store', default=None,
        help="Directory where measurement data is saved.")
    args = parser.parse_args()
    
    ds = DataSink(file_path=args.dir)
    try:
        ds.start()
    except KeyboardInterrupt:
        ds.stop()

if __name__ == '__main__':
    main()