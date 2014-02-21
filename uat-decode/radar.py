#!/usr/bin/python

# This file (C) David Carr 2012.
# All rights reserved.

import cairo
import math
import re
import sys

def draw(blocks_m, blocks_h):
    PPD = 60.0 #pixels per degree
    lat_start = 20
    lat_range = 35
    lon_start = 230
    lon_range = 70

    pixels_x, pixels_y = lon_range*PPD, lat_range*PPD
    
    surface = cairo.ImageSurface (cairo.FORMAT_ARGB32, pixels_x, pixels_y)
    ctx = cairo.Context (surface)

    ctx.scale (pixels_x/lon_range, -1*pixels_y/lat_range) # Normalizing the canvas
    ctx.translate(-lon_start, -(lat_start+lat_range))

    ctx.set_source_rgb(0, 0, 0)
    ctx.rectangle(lon_start, lat_start, lon_range, lat_range)
    ctx.fill()

    #draw in accending resolution
    for block, runs, scale in blocks_m:
        draw_block(ctx, block, runs, scale)

    for block, runs, scale in blocks_h:
        draw_block(ctx, block, runs, scale)

    surface.write_to_png ("test.png") # Output to PNG
         
def draw_block(ctx, block_num, runs, scale):
    blon = (block_num % 450)*0.8
    blat = (block_num / 450)*0.0666666667
    
    #decode runs into bins
    bins = []
    for run in runs:
        length = run[0]
        intensity = run[1]
        for j in xrange(length):
            bins.append(intensity)
    assert(len(bins) == 128)

    #draw bins
    for i in xrange(128):
        if bins[i] > 0:
            lon = blon + (i%32)*0.025*scale
            lat = blat + 0.0666666667*scale - ((i/32)*0.0166666667*scale)
            if bins[i] >= 4:
                ctx.set_source_rgba(1, 0, 0, 1)
            elif bins[i] >= 3:
                ctx.set_source_rgba(1, 1, 0, 1)
            elif bins[i] >= 2:
                ctx.set_source_rgba(0, 1, 0, 1)
            else:
                ctx.set_source_rgba(0, 0, 1, 1)
            ctx.rectangle(lon, lat, 0.025*scale, 0.0166666667*scale)
            ctx.fill()

### MAIN
filename = sys.argv[1]
print "Opening file %s" % filename
infile = open(filename)

#Parse output
lines = infile.readlines()
blocks_m = []
blocks_h = []
i=0
while i<len(lines):
    if lines[i][0:25] == "Global block NEXRAD CONUS":
		#print "Block start"
		#Resolution
		i+=1
		scale = 0
		if re.search("High", lines[i]):
			scale = 1
		elif re.search("Medium", lines[i]):
			scale = 5
		elif re.search("Low", lines[i]):
			scale = 9

		#Encoding --- RLE or Empty
		i+=1
		if re.search("RLE encoding", lines[i]):

			#Block number
			i+=1
			m = re.search("Block (?P<num>[\d]+)", lines[i])
			if m == None:
				print "Wierd"
				continue
			block_num = int(m.group("num"))

			#ignore lat/lon
			i+=1

			#Grab runs
			i+=1
			runs = []
			for j in xrange(0,128):
				if lines[i][0:11] == "[End block]":
				    #print "END"
				    if scale == 1:
				        blocks_h.append((block_num, runs, scale))
				    elif scale == 5:
				        blocks_m.append((block_num, runs, scale))
				    else:
				        print "Hmm. Low resolution blocks not supported."
				    break

				#print lines[i],

				s = "Run: (?P<rl>[\d]+)[\s]+intensity: (?P<intensity>[\d]+)"
				m = re.search(s, lines[i])
				if m:
				    runs.append((int(m.group("rl")), int(m.group("intensity"))))
				else:
				    print "Strange"
				    pass
				i+=1
		#Bitmap encoding "empty" block
		else:
			while True:
				#Block number
				i+=1
				if lines[i][0:11] == "[End block]":
					break
				else:	
					m = re.search("Block (?P<num>[\d]+)", lines[i])
					if m == None:
						continue
					block_num = int(m.group("num"))
					#print block_num
					
					if scale == 1:
						blocks_h.append((block_num, [(128, 1)], scale))
					elif scale == 5:
						blocks_m.append((block_num, [(128, 1)], scale))
					else:
						print "Hmm. Low resolution blocks not supported."
					
    i+=1

draw(blocks_m, blocks_h)
