Introduction

This is a set of software tools that decode 978 MHZ ADS-B UAT data streams.  All contained files are licensed under the GPL V3, which is included for reference.

Getting Started

1. Build and install Forward Error Correction library
- cd fec/fec-3.0
- make
- [sudo] make install
- cd ../../

2. Build UAT decoder
- make

3. Fetch and uncompress example data file
- wget https://www.dropbox.com/s/ilp21hlf7gl6wbj/out-glo-3.gz (or download with web browser)
- gunzip out-glo-3.gz

4. Run decoder
- ./decode out-glo-3 > data_out  (This may take a minute.)
- decoded UAT data stream is now in human readable format in data_out

5. Decode FIS-B radar data from data stream
- ./radar.py data_out
- decoded high resolution radar image of NE US is in out.png.  The decoder can decode full US radar maps, but the short example data file does not contain the full image.

Raw Data Format

The example data stream was recorded near Gloucester, MA.  It consists of a stream of 8-bit integers containing either the value 0 or 1.  (eg: 0x00, 0x01, 0x01, 0x00).  These were sampled at 6.25MHz from the output an FSK receiver tuned to 978MHz.  This corresponds to six samples per UAT datastream bit.  (eg: The UAT stream for '1' would be 0x01, 0x01, 0x01, 0x01, 0x01, 0x010)  The values may be inverted from the specification (I cannot remember).

