#!/usr/bin/python3

import serial # pip3 install pyserial
import argparse
import time
import scipy.signal
from rtlsdr import RtlSdr # pip3 install pyrtlsdr
import numpy as np
import matplotlib.pyplot as plt
import csv 

def isFloat(string):
    try:
        float(string)
        return True
    except ValueError:
        return False

def set_pll(s, freq, on=1, power=1, wait_for_ok=True):
    s.flushInput()
    cmd = str(on) + ' ' + str(freq) + ' ' + str(power) + '\n';
#    print('sending ' + cmd)
    s.write(str.encode(cmd))
#    line = s.readline()
#    print(line)
#    line = s.readline()
#    print(line)
#    if wait_for_ok:
#        ack_ok = False
#        while not ack_ok:
#            line = s.readline()
#            #print(line)
#            if line == b'ok\n':
#                ack_ok = True

def sdr_get_power(sdr):
	"""Measures the RMS power with a RTL-SDR.
	"""
	samples = sdr.read_samples(1024*16)
	freq,psd = scipy.signal.welch(samples,sdr.sample_rate/1e6,nperseg=8192,return_onesided=0, window='flattop')
	psd = 10*np.log10(np.sqrt(psd**2));
	freq += sdr.center_freq/1e6
	return freq,psd;

def readCalibrationFile(path, index):
    if path is not None:
        cal_file = dict()
        with open(path, newline='') as csvfile:
            csvreader = csv.reader(csvfile, delimiter='\t')
            for row in csvreader:
                if not isFloat(row[0]): continue
                f = float(row[0])
                power = float(row[index])
                cal_file[f] = power
        return cal_file
    return None

def sdr_init(index, freq, gain, sample_rate=2.4e6):
    sdr = RtlSdr(device_index = index)
    sdr.sample_rate = 2.4e6
    sdr.center_freq = freq * 1e6
    sdr.gain = gain
    sdr.set_agc_mode(0)
    sdr_get_power(sdr) #First read doesn't work
    return sdr

def sdr_measure(sdr, f, cal_val, f_range = 1, nb_meas = 5):
    sdr.center_freq = f * 1e6
    avg = []
    for j in range(nb_meas):
        freq, psd = sdr_get_power(sdr)
        max_p = np.min(psd)
        for i in range(len(freq)):
            max_p = psd[i] if (f-f_range < freq[i] < f+f_range and psd[i] > max_p) else max_p;
        avg.append(max_p)
    avg = np.mean(avg)
    if cal_val is not None: avg -= cal_val[f]
    return avg

def main():   
    pass;
    
parser = argparse.ArgumentParser(description='EMI mapping with 3D-printer and RTL-SDR.')
parser.add_argument('-p', '--serial-port', type=str, help='serial port',default='/dev/ttyUSB0')
parser.add_argument('-b', '--baud-rate', type=int, help='serial baud rate',default=9600)
parser.add_argument('-l', '--frequency-lbound', type=float, help='',default=1000)
parser.add_argument('-s', '--frequency-step', type=float, help='',default=1)
parser.add_argument('-r', '--frequency-span', type=float, help='',default=300)
parser.add_argument('-g', '--gain', type=int, help='sets the SDR gain in 0.1dB',default=0)
parser.add_argument('-t', '--thru', type=str, help='Input file of a thru measurement')
parser.add_argument('-o', '--open', type=str, help='Input file of an open/short measurement')
parser.add_argument('--invert-sdr', action='store_true', help='Swaps the S11 and S21 SDRs')
args = parser.parse_args()

# Args

s11_listen = len(RtlSdr.get_device_serial_addresses()) > 1
#if not s11_listen:
#    print("-> Running in single device mode (S21 only)")
#else:
#    print("-> Running in dual device mode (S11 and S21)")

# SDR stuff
freq_lbound = args.frequency_lbound ;
freq_range = args.frequency_span;
freq_ubound = freq_lbound + freq_range;
freq_step = args.frequency_step;
frequencies = np.arange(freq_lbound,freq_ubound,freq_step)

# Open serial port
s = serial.Serial(args.serial_port, args.baud_rate, timeout=1)
time.sleep(2) # Wait to boot

# Calibration (Open/Short, (Load), Thru)
cal_s11 = readCalibrationFile(args.open, 2) # O/S  -> S11 = 0 dB 
cal_s21 = readCalibrationFile(args.thru, 1) # Thru -> S21 = 0 dB

# Open SDRs
sdr_S21_index = 0 if not args.invert_sdr else 1
sdr_S11_index = 1 if not args.invert_sdr else 0
sdr_S21 = sdr_init(sdr_S21_index, freq_lbound * 1e6, args.gain)
if s11_listen:
    sdr_S11 = sdr_init(sdr_S11_index, freq_lbound * 1e6, args.gain)
    

s11 = []
s21 = []

print('Frequency\tS21\tS11')
for f in frequencies:
    print(f, end="\t", flush=True)
    
    set_pll(s,f)
    
    # S21
    tmp = sdr_measure(sdr_S21, f, cal_s21)
    s21.append(tmp)    
    print(tmp, end="\t", flush=True)
    
    # S11
    if s11_listen:
        tmp = sdr_measure(sdr_S11, f, cal_s11)
        s11.append(tmp)    
        print(tmp, flush=True)
    else:
        print(0, flush=True)
                
#s21 = s21 - np.max(s21)
plt.plot(frequencies, s21, label="S21")
if s11_listen:
    #s11 = s11 - np.max(s11)
    plt.plot(frequencies, s11, label="S11")
plt.grid(True)
plt.legend(loc='lower right')
plt.xlim([freq_lbound,freq_ubound])
#plt.ylim([None,0])
plt.show()    
    
# Close ressources
set_pll(s,f,0,0)
s.close()
sdr_S21.close()
if s11_listen:
    sdr_S11.close()

if __name__== "__main__":
  main()
