/*************************************************************************
 * gzrecover - A program to recover data from corrupted gzip files
 *
 * Copyright (c) 2002-2013 Aaron M. Renn (arenn@urbanophile.com)
 * 2019 Giovanni M. de Castro
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

#define VERSION "0.90"

/* Global contants */
#define DEFAULT_INBUF_SIZE (1024*1024)
#define DEFAULT_OUTBUF_SIZE (64*1024)

static const char *optstring = "ho:psVv";
static const char *usage = "Usage: gzrecover [-hpsVv] [-o <outfile>] [infile]";

/* Global Variables */
static int split_mode = 0;
static int verbose_mode = 0;
static int outfile_specified = 0;
static int stdout_specified = 0;
static char *user_outname;
static size_t inbuf_size = DEFAULT_INBUF_SIZE;
static size_t outbuf_size = DEFAULT_OUTBUF_SIZE;

/* Display usage string and exit */
void
show_usage_and_exit(int exit_status)
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
      if ((rc == -1) && ((errno == EINTR) || (errno == EAGAIN)))
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

  /* Just return standard output if that is specified */
  if (stdout_specified) 
    return STDOUT_FILENO;    

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
    fprintf(stderr, "Opened output file for writing: %s\n", outfile);

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

  rc = inflateInit2(d_stream, 16+MAX_WBITS); /*To decompress a gzip format file with zlib, call inflateInit2 with the windowBits parameter as 16+MAX_WBITS (https://stackoverflow.com/questions/1838699/how-can-i-decompress-a-gzip-stream-with-zlib) */ 
  if (rc != Z_OK) { throw_error("inflateInit2"); }
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
  int64_t opt, rc, rc2, ifd, ofd, founderr=0, foundgood=0;
  ssize_t bytes_read=0, tot_written=0;
  off_t errpos=0, errinc=0, readpos=0;
  char *infile; 
  unsigned char *inbuf, *outbuf;
  z_stream d_stream;

  /* Parse options */
  while ((opt = getopt(argc, argv, optstring)) != -1)
    {
      switch (opt)
        {
          case 'h':
            show_usage_and_exit(0); 
            break;

          case 'o':
            user_outname = optarg;
            outfile_specified = 1;
            break;

          case 'p':
            stdout_specified = 1;
            break;

          case 's':
            split_mode = 1;
            break;

          case 'v':
            verbose_mode = 1;
            break;

          case 'V':
            fprintf(stderr, "gzrecover %s\n", VERSION);
            break;

          default:
            show_usage_and_exit(1);
        }
    }

  /* Either output to stdout (-p) or specify filename (-o) but not both */
  if (outfile_specified && stdout_specified)
    {
      fprintf(stderr, "gzrecover: Cannot specify output filename (-o) and stdout (-p) simultaneously.\n");
      show_usage_and_exit(1);
    }
 
  /* Allocate our read buffer */
  inbuf = (unsigned char *)malloc(inbuf_size);
  if( inbuf == 0 ){ throw_error("malloc") }

  /* Open input file using name or set to standard input if no file 
     specified */
  if (optind == argc)
    {
      infile = "stdin";
      ifd = STDIN_FILENO;
    }
  else
    {
      infile = argv[optind];
      ifd = open(infile, O_RDONLY);
    }
  if( ifd == -1 ){ free(inbuf); throw_error("open") }

  if (verbose_mode)
    fprintf(stderr, "Opened input file for reading: %s\n", infile);

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
        fprintf(stderr, "File is empty\n");
      close(ifd);
      close(ofd);
      free(inbuf);
      free(outbuf);
      return(0);
    }
  readpos = bytes_read;

  init_zlib(&d_stream, inbuf, bytes_read);
  /* Assume there's a valid gzip header at the beginning of the file AND BECAUSE OF THAT we do not skip it */
  /* skip_gzip_header(&d_stream); */

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

          /* If founderr flag is set, this is our first error. So set
           * the error flag, reset the increment counter to 0, and 
           * read more data from the stream if necessary
           */
          if (!founderr)
            {
              founderr = 1;
              errpos = bytes_read - d_stream.avail_in;

              if (verbose_mode)
                fprintf(stderr, "Found error at byte %ld in input stream\n",
                        (int64_t)(readpos - (bytes_read - errpos)));

              if (d_stream.avail_in == 0)
                {
                  bytes_read = read_internal(ifd, inbuf, inbuf_size);
                  if( bytes_read == -1 ){ throw_error("read") }
                  if (bytes_read == 0)
                    break;
                  readpos += bytes_read;

                  errinc = 0;
                  inflateEnd(&d_stream);
                  init_zlib(&d_stream, inbuf, bytes_read);
                  continue;
                }
            }

          /* Note that we fall through to here from above unless we
           * had to do a re-read n the stream. Set the increment the
           * error increment counter, then re-initialize zlib from 
           * the point of the original error + the value of the increment
           * counter (which starts out at 1). Each time through we keep 
           * incrementing one more byte through the buffer until we
           * either find a good byte, or exhaust it and have to re-read.
           */
          inflateEnd(&d_stream);
          ++errinc;

          /* More left to try in our buffer */
          if (bytes_read > (size_t)(errpos+errinc) )
            {
              init_zlib(&d_stream, inbuf+errpos+errinc-2, bytes_read - (errpos+errinc-2)); 
          /* In gzip, there may be headers inside the compressed file (\x1f\x8b\x08\...) and it complains of an error. Going 2 bytes back does the trick to perfectly inflate an uncorrupted 6GB gziped file like zcat [with inflateInit2(strm, 16+MAX_WBITS))] */
            }
          /* Nothing left in our buffer - read again */
          else
            {
              bytes_read = read_internal(ifd, inbuf, inbuf_size);
              if( bytes_read == -1 ){ throw_error("read") }
              if (bytes_read == 0)
                break;
              readpos += bytes_read;

              inflateEnd(&d_stream);
              init_zlib(&d_stream, inbuf, bytes_read);

              /* Reset errpos and errinc to zero, but leave the founderr
                 flag as true */
              errpos = 0;
              errinc = 0;
            }

          continue;
        }

      /* If we make it here, we were able to decompress data. If the 
       * founderr flag says we were previously in an error state, that means
       * we are starting to decode again after bypassing a region of
       * corruption. Reset the various flags and counters. If we are in 
       * split mode, open the next increment of output files.
       */
      if (founderr & !foundgood)
        {
          foundgood = 1;
          founderr = 0;
          errinc = 0;

          if (verbose_mode)
            fprintf(stderr, "Found good data at byte %ld in input stream\n",
                    (int64_t)(readpos - (bytes_read - d_stream.avail_in)));

          if (split_mode)
            {
              close(ofd);
              ofd = open_outfile(infile);
            }
        }

      /* Write decompressed output - should really handle short write counts */
      rc2 = write(ofd, outbuf, outbuf_size - d_stream.avail_out);
      if ( rc2 == -1 ){ throw_error("write") }
      tot_written += rc2;

      /* We've exhausted our input buffer, read some more */
      if (d_stream.avail_in == 0)
        {
          bytes_read = read_internal(ifd, inbuf, inbuf_size);
          if( bytes_read == -1 ){ throw_error("read"); }
          if (bytes_read == 0)
            break;
          readpos += bytes_read;

          errinc = 0;
          d_stream.next_in = inbuf;
          d_stream.avail_in = bytes_read; 
        }

      /* In we get a false alarm on end of file, we need to handle that to.
       * Reset to one byte past where it occurs. This seems to happen
       * quite a bit
       */
      if (rc == Z_STREAM_END)
        {
          off_t tmppos = d_stream.avail_in;

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
    fprintf(stderr, "Total decompressed output = %ld bytes\n", 
            (int64_t)tot_written);

  free(inbuf);
  free(outbuf);

  return(0); 
}
