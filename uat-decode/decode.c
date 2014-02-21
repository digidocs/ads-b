/*
 * This file (C) David Carr 2012.
 * All rights reserved.
 */

#define _FILE_OFFSET_BITS 64

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "fec.h"
#include "dlac.h"

//even please
#define DATA_LEN 16384

#define SYNC_THRESHOLD_L 20
#define SYNC_THRESHOLD_H (216-SYNC_THRESHOLD_L)

//popcnt stuff - here so that it can be inlined
//Look up table for 8bit popcnt
static uint8_t pop8[256] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};

int popcnt32(uint32_t v)
{
  int c;
  
  // Option 1:
  c = pop8[v & 0xff] + 
    pop8[(v >> 8) & 0xff] + 
    pop8[(v >> 16) & 0xff] + 
    pop8[v >> 24]; 
  
  return c;
}

uint32_t sync_bits[7] = { 0b11000000000000000000111111000000,
			  0b11110000000000001111111111111111,
			  0b11111100000011111100000000000011,
			  0b00111111111111111111000000111111,
			  0b11110000000000001111111111110000,
			  0b11111100000011111100000011111111,
			  0b111111111111111111000000 };
//-----------^ This bit is the first one transmitted

//bit shift register
uint32_t bits[7] = { 0 };

int bytes_left = 0;
int sample_count;
int high_samples;
int bit_count;
uint8_t cur_byte;

//432 bytes data + 120 bytes FEC
#define FRAME_LEN 552
#define MSG_LEN 432
uint8_t msg_buf[FRAME_LEN];
int buf_index; //used in deinterleaving

//stats
int errors = 0;
int sync_l = 0;
int bad_packets = 0;
int no_tele = 0;
int tele_miss = 0;
long int file_byte = 0;

int fec_msg();
void decode_msg();
void bprint(int x);

void data_cb(uint8_t* buf, int len)
{
  static int tele_match = 0;

  //unpack LSB (rest zero)
  //correlate against match pattern
  //find hits
  unsigned int i;
  for (i=0; i<len; i++)
    {
      if (bytes_left == 0)
	{
	  //shift bits
	  bits[6] = bits[6] << 1;
	  bits[6] |= (bits[5] >> 31) & 0x1; //carry high bit
	  bits[6] &= 0xFFFFFF; //whack bits above 216
	  
	  bits[5] = bits[5] << 1;
	  bits[5] |= (bits[4] >> 31) & 0x1; 

	  bits[4] = bits[4] << 1;
	  bits[4] |= (bits[3] >> 31) & 0x1; 

	  bits[3] = bits[3] << 1;
	  bits[3] |= (bits[2] >> 31) & 0x1;
      
	  bits[2] = bits[2] << 1;
	  bits[2] |= (bits[1] >> 31) & 0x1;

	  bits[1] = bits[1] << 1;
	  bits[1] |= (bits[0] >> 31) & 0x1;

	  bits[0] = bits[0] << 1;
	  bits[0] |=  buf[i] & 0x1; //load incoming data

	  //printf("3x%x 2x%x 1x%x 0x%x\n", bitsA[3], bitsA[2], bitsA[1], bitsA[0]);

	  //correlate bits against sync pattern
	  int score = popcnt32(bits[6] ^ sync_bits[6]);
	  score += popcnt32(bits[5] ^ sync_bits[5]);
	  score += popcnt32(bits[4] ^ sync_bits[4]);
	  score += popcnt32(bits[3] ^ sync_bits[3]);
	  score += popcnt32(bits[2] ^ sync_bits[2]);
	  score += popcnt32(bits[1] ^ sync_bits[1]);
	  score += popcnt32(bits[0] ^ sync_bits[0]);
    
	  //printf("score: %d\n", score);
      
	  //printf("%d\n", buf[0]);
	  //printf("%d\n", buf[1]);
	  //printf("%d\n", buf[2]);

	  if (score < SYNC_THRESHOLD_L)
	    {
	      //printf("SYNC LOW! score: %d (%lu) \n", score, file_byte+i);
	      printf("SYNC LOW! score: %d\n", score);
	      bytes_left = FRAME_LEN;
	      sample_count = 0;
	      high_samples = 0;
	      bit_count = 0;
	      buf_index = 0;
	      sync_l++;

	      //check to see if the telescoping decoder will also match
	      tele_match = 0;
	      //16 bits
	      if (popcnt32((sync_bits[5] & 0xFFFF0000) ^ (bits[5] & 0xFFFF0000)) < 4)
		{
		  if (popcnt32(sync_bits[5] ^ bits[5]) < 8)
		    {
		      tele_match = 1;
		    }
		}

	      if (tele_match != 1)
		{
		  printf("NO TELE MATCH\n");
		  no_tele++;
		}
	     
	      #if 0
	      bprint((sync_bits[5] ^ bits[5]) & 0xFFFF0000);
	      printf("\n");

	      bprint(sync_bits[5] ^ bits[5]);
	      printf("\n");
	      
	      printf("score: %d\n", score);
	      #endif

	      #if 0
	      bprint(sync_bits[6] ^ bits[6]);
	      printf(" xor\n");

	      bprint(sync_bits[6]);
	      printf(" syncbits\n");

	      bprint(bits[6]);
	      printf(" bits\n");

	      bprint(0xffda81);
	      printf(" hex test\n");
	      printf("0x%x hex\n", bits[6]);
	      //assert(bits[6] < 0xFFFFFF);

	      #endif

	      //clear shift register
	      bits[6] = bits[5] = bits[4] = bits[3] = 0;
	      bits[2] = bits[1] = bits[0] = 0; 
	      
	      //0b11111100000011111100000011111111
	      //bprint(0b11111100000011111100000011111111);
	      //printf("\n");
	      //bprint(sync_bits[5]);
	      //printf("\n");

	      /*bprint(bits[6]);
	      bprint(bits[5]);
	      printf("\n");
	      bprint(bits[4]);
	      bprint(bits[3]);
	      printf("\n");
	      bprint(bits[2]);
	      bprint(bits[1]);
	      printf("\n");*/
	      
	    }
	  else if (score > SYNC_THRESHOLD_H)
	    {
	      printf("SYNC HIGH! score: %d\n", score);
	    }
	}
      else //synced
	{
	  if (sample_count == 6) //captured six samples --- determine bit
	    {
	      if (high_samples > 3 ) //3 works better than 2
		cur_byte |= (1<<(7-bit_count));
	      else
		cur_byte &= ~(1<<(7-bit_count));
	      
	      sample_count = 0;
	      high_samples = 0;
	      bit_count++;
	    }

	  //captured 8 bits, deinterleave, write a byte into the buffer
	  if (bit_count == 8)
	    {
	      //write data into buffer
	      msg_buf[buf_index] = cur_byte;
	      //increment index
	      buf_index += 92;
	      //wrap index
	      if (buf_index >= FRAME_LEN)
		buf_index -= 551;
	      
	      bit_count = 0;
	      bytes_left--;

	      //see if we're done
	      if (bytes_left == 0)
		{
		  if (fec_msg() >= 0) //don't decode bad messages
		    {
		      //create signature by XORing message together
		      uint8_t sig = 0;
		      int i;
		      for (i=0; i<MSG_LEN; i++)
			sig ^= msg_buf[i];
		      printf("Sig: 0x%02x\n", sig);

		      decode_msg();
		      //check to see if the tele decoder would have missed
		      //a good packet
		      if (tele_match != 1)
			tele_miss++;
		    }
		  else
		    {
		      bad_packets++;
		      printf("[Bad packet]\n\n");
		    }
		}
	    }

	  //take sample and invert
	  if (!(buf[i] & 0x1)) //high bit
	    high_samples++;

	  sample_count++;
	}
      file_byte++;
    }


}

void bprint(int x)
{
  int i;
  for (i=31; i>=0; --i)
    {
      if ((x>>i) & 0x01)
	printf("1");
      else
	printf("0");
    }
}

void* rs_ptr; //pointer to rs decoder info

int fec_msg()
{
  int i;
  
  //first 72 bytes are in correct place
  //dump first 72 bytes and parity
  int errs;
  errs = decode_rs_char(rs_ptr, &msg_buf[0], NULL, 0);
  errors += errs;
  //printf("A errors: %d\n", errs);
  if (errs < 0)
    return -1;

  //next 72 bytes need to be shifted forward 20
  errs = decode_rs_char(rs_ptr, &msg_buf[92], NULL, 0);
  errors += errs;
  //printf("B errors: %d\n", errs);
  if (errs < 0)
    return -1;
  for (i=92; i<92+72; i++)
    msg_buf[i-20] = msg_buf[i];
  
  //next 72 bytes need to be shifted forward 40
  errs = decode_rs_char(rs_ptr, &msg_buf[184], NULL, 0);
  errors += errs;
  //printf("C errors: %d\n", errs);
  if (errs < 0)
    return -1;
  for (i=184; i<184+72; i++)
    msg_buf[i-40] = msg_buf[i];
  
  //next 72 bytes need to be shifted forward 60
  errs = decode_rs_char(rs_ptr, &msg_buf[276], NULL, 0);
  errors += errs;
  //printf("D errors: %d\n", errs);
  if (errs < 0)
    return -1;
  for (i=276; i<276+72; i++)
    msg_buf[i-60] = msg_buf[i];

  //next 72 bytes need to be shifted forward 80
  errs = decode_rs_char(rs_ptr, &msg_buf[368], NULL, 0);
  errors += errs;
  //printf("E errors: %d\n", errs);
  if (errs < 0)
    return -1;
  for (i=368; i<368+72; i++)
    msg_buf[i-80] = msg_buf[i];

  //next 72 bytes need to be shifted forward 100
  errs = decode_rs_char(rs_ptr, &msg_buf[460], NULL, 0);
  errors += errs;
  //printf("F errors: %d\n", errs);
  if (errs < 0)
    return -1;
  for (i=460; i<460+72; i++)
    msg_buf[i-100] = msg_buf[i];

  return 0;
}

void decode_dlac(uint8_t* dlac_buf, int length)
{
  int i=0; //byte index of first character
  int state = 0;
  int value = -1;
  
  int last_was_tab = 0;

  //decode DLAC characters
  while (i<length)
    {
      switch(state)
	{
	case 0:
	  value = (dlac_buf[i]>>2) & 0x3F;
	  state = (state+1)%4;
	  break;
	case 1: 
	  value = ((dlac_buf[i] & 0x03)<<4)
	    + ((dlac_buf[++i]>>4) & 0x0F);
	  state = (state+1)%4;
	  break;
	case 2:
	  //printf("[0x%02x %02x]", dlac_buf[i], dlac_buf[i+1]);
	  value = ((dlac_buf[i] & 0x0F)<<2)
	    + ((dlac_buf[++i]>>6) & 0x03);
	  //printf("[%d %d]", ((dlac_buf[i] & 0x0F)<<2), ((dlac_buf[++i]>>6) & 0x03));
	  state = (state+1)%4;
	  break;
	case 3:
	  value = dlac_buf[i] & 0x3F;
	  i++;
	  state = (state+1)%4;
	  break;
	}

      //handle special case for TAB character
      if (value == 28)
	{
	  //don't print anything and set flag
	  last_was_tab = 1;
	}
      else if (last_was_tab == 1)
	{
	  //value is number of spaces to print
	  int sp;
	  for (sp=0; sp<value; sp++)
	    printf(" ");

	  last_was_tab = 0;
	}
      else //normal --- print character
	printf("%c", dlac[value]);

    }
  printf("[END DLAC]\n");
}

void decode_64(uint8_t* payload, int length)
{
  printf("Global block NEXRAD CONUS\n");

	int block_num = ((payload[0] & 0x0F)<<16) + (payload[1]<<8) + payload[2];
	int res = (payload[0]>>4) & 0x03;

	if (res == 0)
		printf("High resolution\n");
	  else if (res == 1)
		printf("Medium resolution\n");
	  else if (res == 2)
		printf("Low resolution\n");

	if (payload[0] & 0x80)
	{
	  printf("RLE encoding\n");
	 
	  printf("Block %d\n", block_num);
	  
	  //FIXME only valid below 60 latitude
	  printf("Latitude:\t%f\t", (block_num / 450)*0.0666666667);
	  printf("Longitude:\t%f\n", (block_num % 450)*0.8-360.0);

	  int i;
	  for (i=3; i<length; i++)
		{
		  int run_len = ((payload[i]>>3) & 0x1F) + 1;
		  int intensity = payload[i] & 0x07;
		  printf("Run: %d\tintensity: %d\n", run_len, intensity);
		}
	}
	else
	{
		printf("Empty encoding\n");
		
		//print empty blocks
		printf("Block %d\n", block_num);
		
		int length = payload[3] & 0x0F;
		
		//printf("%02x %02x %02x\n", payload[3], payload[4], payload[5]);
		//printf("L %d\n", length);
		
		//special case because first byte is non-standard (of course)
		int bb0 = (payload[3] >> 4) & 0x0F;
		//printf("BB00: %02x\n", bb0); 
		int j;
		for (j=0; j<4; j++)
			if (bb0 & (0x01<<j))
				printf("Block %d\n", block_num + j + 1);
		
		//the rest
		int i;
		for (i=0; i<length; i++)
		{
			int bbx = payload[4+i];
			//printf("BB%02d: %02x\n", i+1, bbx);

			for (j=0; j<8; j++)
				if (bbx & (0x01<<j))
					printf("Block %d\n", block_num + length*8 + j - 3);
		}
	}
  
  printf("[End block]\n");
}

void decode_dromespace(uint8_t* payload, int length)
{
  //int format = (payload[0] & 0xF0)>>4;
  int format = (payload[0]>>4) & 0x0F;
  int version = (payload[0] & 0x0F);
  int count = (payload[1] & 0xF0)>>4;
  int spare = (payload[1] & 0x0F);

  printf("Aerodrome/space - format: %d, version: %d, count: %d, spare: %d, location: ",
	 format, version, count, spare);
  decode_dlac(&payload[2],3);

  //DLAC text with header
  if (format == 2)
    {
      int length = (payload[6]<<8) + payload[7];
      int num = (payload[8]<<6) + (payload[9]>>2);
      printf("num: %d\n", num);
      decode_dlac(&payload[11], length-6);
      printf("\n");
    }
  //Graphical overlay
  else if (format == 8)
    {
      int length = (payload[6]<<2) + (payload[7]>>6);
      int num = ((payload[7] & 0x3F)<<8) + (payload[8]);
      int year = (payload[9]>>1);
      //seems broken
      int record_id = ((payload[9] & 0x01)<<3) + ((payload[10] & 0xE0)>>5);
      printf("0x%02x, 0x%02x,  0x%02x, 0x%02x, 0x%02x,\n", 
	     payload[6], payload[7],  payload[8],  payload[9], payload[10]);
      printf("Graphical overlay length: %d, num: %d\n, yr: %d, rec_id: %d \n",
	     length, num, year, record_id);
    }

  printf("[End block]\n");
}

void decode_apdu(uint8_t* apdu, int length)
{
  //parse APDU header
  int a_bit = (apdu[0]>>7) & 0x01; 
  int g_bit = (apdu[0]>>6) & 0x01; 
  int p_bit = (apdu[0]>>5) & 0x01; 
	  
  int pid = ((apdu[0] & 0x1F)<<6) + ((apdu[1] & 0xFC)>>2);

  int s_flag = (apdu[1]>>1) & 0x01;
  int t_flag = ((apdu[1] & 0x01)<<1) + ((apdu[2]>>7) & 0x01);

  printf("APDU pid: %d, A: %d, G: %d, P: %d, S: %d, T: %d\n",
	 pid, a_bit, g_bit, p_bit, s_flag, t_flag);

  //shortest header
  uint8_t* data_start = &apdu[4];

  //shortest + day and month
  if (t_flag == 2)
    data_start += 1;

  //FIXME --- only implemented for t_flag == 0
  //decode time
  if (t_flag == 0)
    {
      int hours = (apdu[2]>>2) & 0x1F;
      int minutes = ((apdu[2] & 0x03)<<4) + ((apdu[3]>>6) & 0x0F);
      printf("Hours: %d, Min.: %d\n", hours, minutes);
    }

  //calculate length of whats left
  int payload_length = (length-4) - (data_start - &apdu[4]);

  //decode type 63/64 - Global Block NEXRAD
  if (pid == 63 || pid == 64)
    {
      decode_64(data_start, length-4);
    }
  //decode type 413
  else if (pid == 413)
    {
      decode_dlac(data_start, length-4);
    }
  else if (pid == 8 || pid == 11 || pid == 13)
    {
      decode_dromespace(data_start, length-4);
    }
 
  //REMOVE
  //scan for HDLC escape sequence 
  /*int i;
  for (i=0; i<length; i++)
    {
      if (apdu[i] == 0x7D)
	printf("ESCAPE\n");
	}*/
     
}

//decodes an information frame pointed to by frame
//returns frame data length
int decode_iframe(uint8_t* frame)
{
  //read first information frame length
  int length = (frame[0]<<1) + (frame[1]>>7);
  printf("\nIFrame length: %d\n", length);
  
  if (length == 0)
    return 0;

  //see if this is an APDU frame
  int type = frame[1] & 0x0F;
  if (type == 0)
    {
      decode_apdu(&frame[2], length);
    }
  else if (type == 15)
    {
      printf("TIS-B frame\n");
    }
  
  return length;
}

void decode_msg()
{
  int i;

  //lat lon
  //printf("LAT %02x %02x %02x\n", msg_buf[0], msg_buf[1], msg_buf[2]);
  //printf("LON %02x %02x %02x\n", msg_buf[3], msg_buf[4], msg_buf[5]);
  int lat = ((msg_buf[0]&0x80)<<16) + (msg_buf[0]<<15) + (msg_buf[1]<<7) + (msg_buf[2]>>1);
  int lon = ((msg_buf[2]&0x01)<<23) + (msg_buf[3]<<15) + (msg_buf[4]<<7)
    + (msg_buf[5]>>1);
  float to_deg = 2.145767212e-5;
  printf("LAT %f\t", lat * to_deg);
  printf("LON %f\n", lon * to_deg - 360);

  //see if message has valid data
  if (msg_buf[6] & 0x20)
    {
      //printf("Valid Data\n");
  
      uint8_t* frame_ptr = &msg_buf[8];

      while (frame_ptr < &msg_buf[MSG_LEN])
	{
	  int length = decode_iframe(frame_ptr); 
	  if (length == 0)
	    break;
	  else
	    frame_ptr += (length + 2);
	}
      printf("[END SYNC]\n\n");
    }
  
  for(i=0; i<MSG_LEN; i++)
    {
      //printf("0x%02x \t%d\n", msg_buf[i], i);
    }
}

int main (int argc, char *argv[])
{
  unsigned int i;
  
  uint8_t buf[DATA_LEN];

  FILE* f = fopen(argv[1], "r");
  if (f < 0)
    {
      printf("Open returned: %d\n", (int)f);
    }

  //initialize RS decoder
  int n = 92;
  int k = 72;
  rs_ptr = init_rs_char(8, 391, 120, 1, (n-k), 255-n);

  while(feof(f) == 0)
    {
      int len = fread(buf, 1, DATA_LEN, f);
     
      data_cb(buf, len);
    }

  printf("Sync lows: %d, Good packets: %d, No tele: %d, Tele miss: %d, Bad packets: %d, errors: %d\n",
	 sync_l, sync_l-bad_packets, no_tele, tele_miss, bad_packets, errors);
	
  return 0;
}
