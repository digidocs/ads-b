Introduction

This directory contains the schematic, PCB design, and some documentation for a ML2722 based UAT receiver.

Theory of operation

The 978 MHz signal from the antenna enters from CONN1 and passes through an impedance matching network formed by L2 and C4.  A ML2722 FSK receiver IC originally designed for use with cordless phones is used to decode the 1.0417Mbps FSK signal.  The ML2722 is not officially specified to operate at 978MHz, but it appears to operate well in this range.  Internally, the ML2722 downconverts and demodulates the incoming 978MHz signal, however the internal bit slicer is disabled because its time constant is not appropriate for the UAT datastream.  Instead, the raw discriminator output (availble in test mode) is fed through a low pass filter formed by L1 and C27 and then on to the TS3021 comparator.  A MCP4725 I2C DAC is used to provide an adjustable reference level to the comparator for accurate bit slicing.  The output of the DAC which is the raw FSK datastream is then output via J1.  J1 is intended to be connected to a USB capture device sampling the 1-bit value at 6.25Msps.  Here a Cypress EZ-USB FX2LP was used to perform this function.


