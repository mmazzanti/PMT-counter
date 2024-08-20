import serial
import argparse
import re
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D 
from matplotlib.animation import FuncAnimation
import numpy as np
import time
import sys


x_list = []
y_list = []
MAX_LEN = 100
nsample = 0
Y_MAX = 50
BINNING = 5
avg_counts=[]
integrated = 0


props = dict(boxstyle='round', facecolor='wheat', alpha=0.5)
#plt.rcParams['animation.html'] = 'jshtml'


fig, axs = plt.subplots(1,2,figsize=(25, 4),gridspec_kw={'width_ratios': [10, 1]})
axs[0].set_title("PMT Counts vs. Time")
axs[0].set(xlabel='sample', ylabel='Counts')
axs[0].set_ylim(0, Y_MAX)
axs[0].grid(1)

graph = axs[0].plot(x_list,y_list)[0]

axs[1].set_title("Counts/Batch")
graph2 = axs[1].hist(avg_counts, bins=10)[2]

PMTline = Line2D([0],[0.0])
axs[0].add_line(PMTline)


def send_init(serial):
        # Send the command to the serial port
        serial.write(b"INIT\r\n")

def setup_PMT(serial, bins, exposure):
        cmd = "SET "+ str(bins) + " " + str(exposure) + "\r\n"
        serial.write(bytes(cmd, encoding='utf8'))

def set_bins(serial, bins):
        # Send the command to the serial port
        serial.write(b"%d\r\n" % bins)

def set_exposure(serial, exposure):
        # Send the command to the serial port
        serial.write(b"%d\r\n" % exposure)

def read_string(serial_port):
        serialString = serial_port.read(serial_port.in_waiting)
        #print(serialString)
        print("---- END ----")
        # Print the contents of the serial data
        asciiString = serialString.decode("Ascii")
        print("---- START ----")
        print(asciiString)
        splittedSTR = asciiString.split("\r\n")
        #print(splittedSTR)
        if len(splittedSTR)>3:
                for str in splittedSTR:
                        data = re.split(r'([0-9]*), ([0-9]*)', str)
                        if len(data) > 1 and len(data[2]) > 0:
                                add_data_to_bin(int(data[2]))
                #print("--- END BATCH ---")
        return True



def init_serial(port):
        ser = serial.Serial(
        port=port,
        baudrate = 576000,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,  
        bytesize=serial.EIGHTBITS,
        timeout=1
)       
        return ser


def add_data_to_bin(data):
        avg_counts.append(data)
        
def on_close(event):
    print('Figure closed, stopping the script')
    sys.exit()

def add_data_to_buffer(x,y):
        if len(x_list) <= MAX_LEN:
                x_list.append(x)
                y_list.append(y)
        else:
                x_list.append(x)
                x_list.pop(0)
                y_list.append(y)
                y_list.pop(0)


if __name__=="__main__": 
        parser = argparse.ArgumentParser(
                        prog='PMT Reader',
                        description='A Python script for counting PMT pulses',
                        epilog='Select the serial port and exposure time')

        parser.add_argument("port", help="Serial reader for PMT counter",
                        type=str)
        parser.add_argument("bins", help="Number of PMT bins (exposure time)",
                        type=int)
        args = parser.parse_args()
        print("Opening RP2040 PMT board on: ", args.port, "\n")
        serialPort = init_serial(args.port)
        # Fix exposure time to 10 cycles (ATM the board does not support this feature)
        setup_PMT(serialPort, args.bins, 10)
        nsample = args.bins
        
        # Connect to the close event
        fig.canvas.mpl_connect('close_event', on_close)
        plt.ylim(0,50)
        plt.pause(0.01)
        integrated = 0
        # the update loop
        while(True):
        # updating the data
                sample = read_string(serialPort)
                if sample:
                        # add some delay when reading from serial after setup
                        time.sleep(0.02)
                        setup_PMT(serialPort, args.bins, 10)
                        if len(avg_counts) >= BINNING:
                                add_data_to_buffer(int(integrated), np.sum(avg_counts))         
                                # removing the older graph
                                graph.remove() 
                                graph2.remove()
                                
                                # plotting newer graph
                                graph = axs[0].plot(x_list, y_list,color = 'g')[0]
                                graph2 = axs[1].hist(avg_counts, bins=10,color = 'b')[2]

                                axs[0].set_xlim(left=max(0, x_list[-1] - 50), right=x_list[-1] + 10)
                                axs[0].set_ylim(np.min(y_list)-10, np.max(y_list)+10)

                                axs[1].set_ylim(0, len(avg_counts))
                                axs[1].set_xlim(np.min(avg_counts)-5, np.max(avg_counts)+5)
                                # calling pause function for 0.2 seconds (to avoid blocking the UI)
                                plt.pause(0.02)
                                avg_counts=[]
                                integrated+=1
                                

                        nsample = args.bins
