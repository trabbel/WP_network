#!/usr/bin/env python3
import csv
#import yaml
from datetime import datetime
from pathlib import Path


class data2csv:

    def __init__(self, file_path, file_name, config_file=None):

        Path(file_path).mkdir(parents=True, exist_ok=True) # make new directory
        self.file_path = file_path
        self.file_name = file_name

        self.csvfile = open(file_path / file_name, 'w')
        self.csvwriter = csv.writer(self.csvfile)
        
        # Data fields are loaded in their original order by default
        # and we always want to add our timestamp.
        header = ['timestamp','differential_potential']
        self.csvwriter.writerow(header)
        self.csvfile.close()

    def close_file(self):
        self.csvfile.close()

    def write2csv(self, data):
        try:
            
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            data4csv = [timestamp,data]
            #print(data4csv)
            self.csvfile = open(self.file_path / self.file_name, 'a')
            self.csvwriter = csv.writer(self.csvfile)
            self.csvwriter.writerow(data4csv)
            self.csvfile.close()
            
        except Exception as e:
            return e