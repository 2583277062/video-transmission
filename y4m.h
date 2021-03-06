/*****************************************************************************
 * y4m.c: y4m input
 *****************************************************************************
 * Copyright (C) 2003-2013 x264 project
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *          Loren Merritt <lorenm@u.washington.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@x264.com.
 *****************************************************************************/
#ifndef __Y4M_H
#define __Y4M_H

#include "stdint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
using namespace std;

extern "C"
{
	#include "x264.h"
}

#define Y4M_MAGIC "YUV4MPEG2"
#define MAX_YUV4_HEADER 80
#define Y4M_FRAME_MAGIC "FRAME"
#define MAX_FRAME_HEADER 80

typedef void * 	hnd_t;

#define FAIL_IF_ERROR( cond, ... ) FAIL_IF_ERR( cond, "y4m", __VA_ARGS__ )


/* YUV4MPEG2 raw 420 yuv file operation */
typedef struct {
	FILE *fh;
	int width, height;
	int next_frame;
	int seq_header_len, frame_header_len;
	int frame_size;
} y4m_input_t;


void x264_reduce_fraction( int *n, int *d )
{
	int a = *n;
	int b = *d;
	int c;
	if( !a || !b )
		return;
	c = a % b;
	while(c)
	{
		a = b;
		b = c;
		c = a % b;
	}
	*n /= b;
	*d /= b;
}

   
int open_file_y4m( char *psz_filename, hnd_t **p_handle, x264_param_t *p_param )
{
	int  i, n, d;
	int  interlaced;
	char header[MAX_YUV4_HEADER+10];
	char *tokstart, *tokend, *header_end;
	y4m_input_t *h = (y4m_input_t*) *p_handle;
   
	h->next_frame = 0;
   
	h->fh = fopen(psz_filename, "rb");
	if( h->fh == NULL )
		return -1;
   
	h->frame_header_len = strlen(Y4M_FRAME_MAGIC)+1;
   
	/* Read header */
	for( i=0; i<MAX_YUV4_HEADER; i++ )
	{
		header[i] = fgetc(h->fh);
		if( header[i] == '\n' )
		{
			/* Add a space after last option. Makes parsing "444" vs "444alpha" easier. */
			header[i+1] = 0x20;
			header[i+2] = 0;
			break;
		}
	}
	if( i == MAX_YUV4_HEADER || strncmp(header, Y4M_MAGIC, strlen(Y4M_MAGIC)) )
		return -1;
   
	/* Scan properties */
	header_end = &header[i+1]; /* Include space */
	h->seq_header_len = i+1;
	for( tokstart = &header[strlen(Y4M_MAGIC)+1]; tokstart < header_end; tokstart++ )
	{
		if(*tokstart==0x20) continue;
		switch(*tokstart++)
		{
		case 'W': /* Width. Required. */
			h->width = p_param->i_width =  strtol(tokstart, &tokend, 10);
			tokstart=tokend;
			break;
		case 'H': /* Height. Required. */
			h->height = p_param->i_height = strtol(tokstart, &tokend, 10);
			tokstart=tokend;
			break;
		case 'C': /* Color space */
			if( strncmp("420", tokstart, 3) )
			{
				fprintf(stderr, "Colorspace unhandled\n");
				return -1;
			}
			tokstart = strchr(tokstart, 0x20);
			break;
		case 'I': /* Interlace type */
			switch(*tokstart++)
			{
			case 'p': interlaced = 0; break;
			case '?':
			case 't':
			case 'b':
			case 'm':
			default: interlaced = 1;
				fprintf(stderr, "Warning, this sequence might be interlaced\n");
			}
			break;
		case 'F': /* Frame rate - 0:0 if unknown */
			if( sscanf(tokstart, "%d:%d", &n, &d) == 2 && n && d )
			{
				x264_reduce_fraction( &n, &d );
				p_param->i_fps_num = n;
				p_param->i_fps_den = d;
			}
			tokstart = strchr(tokstart, 0x20);
			break;
		case 'A': /* Pixel aspect - 0:0 if unknown */
			if( sscanf(tokstart, "%d:%d", &n, &d) == 2 && n && d )
			{
				x264_reduce_fraction( &n, &d );
				p_param->vui.i_sar_width = n;
				p_param->vui.i_sar_height = d;
			}
			tokstart = strchr(tokstart, 0x20);
			break;
		case 'X': /* Vendor extensions */
			if( !strncmp("YSCSS=",tokstart,6) )
			{
				/* Older nonstandard pixel format representation */
				tokstart += 6;
				if( strncmp("420JPEG",tokstart,7) &&
					strncmp("420MPEG2",tokstart,8) &&
					strncmp("420PALDV",tokstart,8) )
				{
					fprintf(stderr, "Unsupported extended colorspace\n");
					return -1;
				}
			}
			tokstart = strchr(tokstart, 0x20);
			break;
		}
	}
   
	fprintf(stderr, "yuv4mpeg: %ix%i@%i/%ifps, %i:%i\n",
			h->width, h->height, p_param->i_fps_num, p_param->i_fps_den,
			p_param->vui.i_sar_width, p_param->vui.i_sar_height);
   
	/**p_handle = (hnd_t*)h;*/
	return 0;
}
   
/* Most common case: frame_header = "FRAME" */
int get_frame_total_y4m( hnd_t handle )
{
	y4m_input_t *h             = (y4m_input_t *)handle;
	int          i_frame_total = 0;
	long         init_pos      = ftell(h->fh);
   
	if( !fseek( h->fh, 0, SEEK_END ) )
	{
		uint64_t i_size = ftell( h->fh );
		fseek( h->fh, init_pos, SEEK_SET );
		i_frame_total = (int)((i_size - h->seq_header_len) /
								(3*(h->width*h->height)/2+h->frame_header_len));
	}
   
	return i_frame_total;
}
   
int read_frame_y4m( x264_picture_t *p_pic, hnd_t handle, int i_frame )
{
	int          slen = strlen(Y4M_FRAME_MAGIC);
	int          i    = 0;
	char         header[16];
	y4m_input_t *h    = (y4m_input_t *)handle;
   
	if( i_frame != h->next_frame )
	{
		if (fseek(h->fh, (uint64_t)i_frame*(3*(h->width*h->height)/2+h->frame_header_len)
					+ h->seq_header_len, SEEK_SET))
			return -1;
	}
   
	/* Read frame header - without terminating '\n' */
	if (fread(header, 1, slen, h->fh) != slen)
		return -1;
       
	header[slen] = 0;
	if (strncmp(header, Y4M_FRAME_MAGIC, slen))
	{
		fprintf(stderr, "Bad header magic (%08X <=> %s)\n",
				*((uint32_t*)header), header);
		return -1;
	}
     
	/* Skip most of it */
	while (i<MAX_FRAME_HEADER && fgetc(h->fh) != '\n')
		i++;
	if (i == MAX_FRAME_HEADER)
	{
		fprintf(stderr, "Bad frame header!\n");
		return -1;
	}
	h->frame_header_len = i+slen+1;
   
	if( fread(p_pic->img.plane[0], 1, h->width*h->height, h->fh) <= 0
		|| fread(p_pic->img.plane[1], 1, h->width * h->height / 4, h->fh) <= 0
		|| fread(p_pic->img.plane[2], 1, h->width * h->height / 4, h->fh) <= 0)
		return -1;
   
	h->next_frame = i_frame+1;
   
	return 0;
}
   
int close_file_y4m(hnd_t handle)
{
	y4m_input_t *h = (y4m_input_t *) handle;
	if( !h || !h->fh )
		return 0;
	return fclose(h->fh);
}


#endif