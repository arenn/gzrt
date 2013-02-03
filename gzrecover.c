/*************************************************************************
 * gzrecover - A program to recover data from corrupted gzip files
 *
 * Copyright (c) 2002-2012 Aaron M. Renn (arenn@urbanophile.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307 USA
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <zlib.h>

/* Global contants */
#define DEFAULT_INBUF_SIZE (1024*1024)
#define DEFAULT_OUTBUF_SIZE (64*1024)

static const char *optstring = "ho:sv";
static const char *usage = "Usage: gzrecover [-hsv] [-o <outfile>] <infile>";

/* Global Variables */
static int split_mode = 0;
static int verbose_mode = 0;
static int outfile_specified = 0;
static char *user_outname;
static size_t inbuf_size = DEFAULT_INBUF_SIZE;
static size_t outbuf_size = DEFAULT_OUTBUF_SIZE;

/* Display usage string and exit */
void
show_usage(int exit_status)
{
  fprintf(stderr, "%s\n", usage);
  exit(exit_status); 
}

#define throw_error(callname) perror(callname); exit(1);

/* Read bytes from a file - restart on EINTR */
ssize_t
read_internal(int fd, void *buf, size_t count)
{
  ssize_t rc = 0;

  for (;;)
    {
      rc = read(fd, buf, count);
      if ((rc == -1) && (errno == EINTR))
        continue;
      return(rc);
    }
}

/* Open output file for writing */
int
open_outfile(char *infile)
{
  int ofd;
  char *outfile, *ptr;
  static int suffix = 1;

  /* Build the output file name */
  if (outfile_specified)
    outfile = (char *)malloc(strlen(user_outname) + 9);
  else
    outfile = (char *)malloc(strlen(infile) + 25); 
  if( outfile == 0 ){ throw_error("malloc") }
    
  if (!outfile_specified) /* Strip of .gz unless user specified name */
   {
     ptr = strstr(infile, ".gz");
     if (ptr)
       *ptr = '\0'; /* Bad form to directly edit command line */ 

     ptr = strrchr(infile, '/'); /* Kill pathname */
     if (ptr)
       infile = ptr+1;
   }

  if (outfile_specified && split_mode)
    sprintf(outfile, "%s.%d", user_outname, suffix++);
  else if (outfile_specified)
    strcpy(outfile, user_outname);
  else if (split_mode)
    sprintf(outfile, "%s.recovered.%d", infile, suffix++);
  else
    sprintf(outfile, "%s.recovered", infile);

  /* Open it up */
  ofd = open(outfile, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
  if( ofd == -1 ){ throw_error("open") }

  if (verbose_mode)
    fprintf(stdout, "Opened output file for writing: %s\n", outfile);

  free(outfile);

  return(ofd);
}

/* Initialize the zlib decompression engine */
void
init_zlib(z_stream *d_stream, unsigned char *buffer, size_t bufsize)
{
  int rc;

  memset(d_stream, 0, sizeof(z_stream));
  d_stream->next_in = buffer;
  d_stream->avail_in = bufsize;

  rc = inflateInit2(d_stream, -15); /* Don't ask why -15 - I don't know */
  if (rc != Z_OK)
    {
      perror("inflateInit2");
      exit(1);
    }
}

/* Skip gzip header stuff we don't care about */ 
void
skip_gzip_header(z_stream *d_stream)
{
  char flags;
  unsigned int len;

  flags = d_stream->next_in[3];
  d_stream->next_in += 10;
  d_stream->avail_in -= 10;

  if ((flags & 0x04) !=0) /* Extra field */
    {
      len = (unsigned int)*d_stream->next_in;
      len += ((unsigned int)*(d_stream->next_in)) << 8;
      d_stream->next_in += (2 + len);
      d_stream->avail_in -= (2 + len);
    }

  if ((flags & 0x08) != 0)  /* Orig Name */
    {
      while(*d_stream->next_in != 0)
        {   
          ++d_stream->next_in;
          --d_stream->avail_in;
        }
      ++d_stream->next_in;
      --d_stream->avail_in;
   }

  if ((flags & 0x10) != 0) /* Comment */
    while(*d_stream->next_in != 0)
      {   
         ++d_stream->next_in;
         --d_stream->avail_in;
      }

  if ((flags & 0x02) != 0) /* Head CRC */
    {
      d_stream->next_in += 2;
      d_stream->avail_in -= 2 ;
    }
}

/* Main program driver */
int
main(int argc, char **argv)
{
  int opt, rc, ifd, ofd, founderr=0, foundgood=0;
  ssize_t bytes_read=0;
  off_t errpos=0, errinc=0;
  char *infile; 
  unsigned char *inbuf, *outbuf;
  z_stream d_stream;

  /* Parse options */
  while ((opt = getopt(argc, argv, optstring)) != -1)
    {
      switch (opt)
        {
          case 'h':
            show_usage(0); 

          case 'o':
            user_outname = optarg;
            outfile_specified = 1;
            break;

          case 's':
            split_mode = 1;
            break;

          case 'v':
            verbose_mode = 1;
            break;

          default:
            show_usage(1);
        }
    }
 
  if (optind == argc)
    show_usage(1);
  infile = argv[optind];

  /* Open input file and memory map */
  inbuf = (unsigned char *)malloc(inbuf_size);
  if( inbuf == 0 ){ throw_error("malloc") }
  ifd = open(infile, O_RDONLY);
  if( ifd == -1 ){ free(inbuf); throw_error("open") }

  if (verbose_mode)
    fprintf(stdout, "Opened input file for reading: %s\n", infile);

  /* Open output file & initialize output buffer */
  ofd = open_outfile(infile);
  outbuf = (unsigned char *)malloc(outbuf_size);
  if( outbuf == 0 ){ throw_error("malloc") }

  /* Initialize zlib */
  bytes_read = read_internal(ifd, inbuf, inbuf_size);
  if( -1 == bytes_read ){ throw_error("read") }
  if (bytes_read == 0)
    {
      if (verbose_mode)
        fprintf(stdout, "File is empty\n");
      close(ifd);
      close(ofd);
      free(inbuf);
      free(outbuf);
      return(0);
    }

  init_zlib(&d_stream, inbuf, bytes_read);
  skip_gzip_header(&d_stream);

  /* Finally - decompress this bad boy */
  for (;;)
    {
      d_stream.next_out = outbuf;
      d_stream.avail_out = outbuf_size; 

      rc = inflate(&d_stream, Z_NO_FLUSH);

      /* Here is the strategy. If we bomb, we reset zlib to one byte past the
       * error location and keep doing it until such time as we are able
       * to start decompressing something.  Alas, this seems to result in
       * a number of false starts.
       */ 
      if ((rc != Z_OK) && (rc != Z_STREAM_END))
        {
          foundgood = 0;
          if (!founderr)
            {
              // FIXME:  errpos not correct
              errpos = inbuf_size - d_stream.avail_in;
              founderr = 1;

              if (d_stream.avail_in == 0)
                {
                  bytes_read = read_internal(ifd, inbuf, inbuf_size);
                  if( bytes_read == -1 ){ throw_error("read") }
                  if (bytes_read == 0)
                    break;

                  errinc = 0;
                  inflateEnd(&d_stream);
                  init_zlib(&d_stream, inbuf, bytes_read);
                  continue;
                }

              if (verbose_mode)
                fprintf(stdout, "Found error at byte %d in input stream\n",
                        (int)errpos);
            }

          inflateEnd(&d_stream);
	  ++errinc;
          if( inbuf_size > (size_t)(errpos+errinc) )
            {
              init_zlib(&d_stream, inbuf+errpos+errinc, inbuf_size - (errpos+errinc));
            }
          else
            {
              bytes_read = read_internal(ifd, inbuf, inbuf_size);
              if( bytes_read == -1 ){ throw_error("read") }
              if (bytes_read == 0)
                break;

              errinc = 0;
              inflateEnd(&d_stream);
              init_zlib(&d_stream, inbuf, bytes_read);
            }

          continue;
        }

      if (founderr & !foundgood)
        {
          foundgood = 1;
          founderr = 0;
          errinc = 0;
	  if (verbose_mode)
            fprintf(stdout, "Found good data at byte %d in input stream\n",
                    (int)(errpos + errinc));

          if (split_mode)
            {
              close(ofd);
              ofd = open_outfile(infile);
            }
        }

      /* Write decompressed output - should really handle short write counts */
      if( -1 == write(ofd, outbuf, outbuf_size - d_stream.avail_out) ){ throw_error("write") }
      fsync(ofd);

      /* We've exhausted our input buffer, read some more */
      if (d_stream.avail_in == 0)
        {
          bytes_read = read_internal(ifd, inbuf, inbuf_size);
          if( bytes_read == -1 ){ perror("read"); exit(1); }
          if (bytes_read == 0)
            break;

          errinc = 0;
          d_stream.next_in = inbuf;
          d_stream.avail_in = bytes_read; 
        }

      /* In we get a false alarm on end of file, we need to handle that to.
       * Reset to one byte past where it occurs */
      if (rc == Z_STREAM_END)
        {
          off_t tmppos = d_stream.avail_in;

          if (verbose_mode)
            fprintf(stdout, "Premature end of stream at %zd\n",
                    inbuf_size - d_stream.avail_in);

          inflateEnd(&d_stream);
          if ((unsigned char *)d_stream.next_in == inbuf)
            {
              init_zlib(&d_stream, inbuf, bytes_read);
            }
          else
            {
              init_zlib(&d_stream, inbuf + (bytes_read - tmppos) + 1, 
                        tmppos + 1);
            }

	  continue;
        }
    }

  inflateEnd(&d_stream);

  /* Close up files */
  close(ofd);
  close(ifd);

  if (verbose_mode)
    fprintf(stdout, "Total decompressed output = %d bytes\n", 
            (int)d_stream.total_out);

  free(inbuf);
  free(outbuf);

  return(0); 
}

