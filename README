gzrecover - Recover data from a corrupted gzip file

gzrecover is a program that will attempt to extract any readable data
out of a gzip file that has been corrupted. 

*****************************************************************************
ATTENTION!!!!  99% of "corrupted" gzip archives are caused by transferring
the file via FTP in ASCII mode instead of binary mode.  Please re-transfer
the file in the correct mode first before attempting to recover from a file
you believe is corrupted.
*****************************************************************************

It is highly likely that not all data in the file will be successfully
retrieved.  In the event that the compressed file was a tar archive, the
standard tar program will probably not be able to extract all of the 
files in the recovered file, so you will need to use GNU cpio instead.

For compilation and installation instructions see README.build

USAGE:

gzrecover [ -hpsVv ] [-o <filename>] [filename] 

If no input filename is specific, gzrecover reads from the standard input.
By default, gzrecover writes its output to <filename>.recovered.  If the
original filename ended in .gz, that extension is removed. The default 
output filename when reading from the standard input is "stdin.recovered". 

Options include:

-o <name> - Sets the output file name
-p        - Write output to standard output for pipeline support
-s        - Splits each recovered segment into its own file,
            with numeric suffixes (.1, .2, etc) (UNTESTED)
-h        - Print the help message
-v        - Verbose logging on
-V        - Print version number

-o and -p cannot be specified at the same time.

Note that gzrecover will run slower than regular gunzip does, but has
been significantly inproved in speed since the last release.  The more 
corruption in the file, the more slowly it runs.

Running gzrecover on an uncorrupted gzip file should simply uncompress it.
However, substituting gzrecover for gunzip on a regular basis is not
recommended.

Any recovered data should be manually verified for validity. There's no 
guarantee anything will be recovered

RECOVERING TAR FILES

If your .gz file is a tar archive, it is likely the recovered file cannot
be processed by the tar program because tar will choke on any errors in 
the file format.  Fortunately, GNU cpio will extract tar files and will
skip any corrupted bytes.  If you don't have GNU cpio on your system,
you can download it from ftp://ftp.gnu.org/pub/gnu/cpio/cpio-2.6.tar.gz
Note that I have only tested with version 2.5 or higher.

To extract files, use the following cpio options:

cpio -F <filename from gzrecover output> -i -v

Note that cpio may spew large amounts of error messages to the terminal,
and may also take a very long time to run on a file that had lots of
corruption.

Note: I previously had patched the GNU tar sources to enable it to
skip corrupted bytes, but that patch has been discontinued because it
is not needed and was only marginally successful at best.

PUTTING IT ALL TOGETHER

Your file foo.tar.gz is on a tape with bad data.  To recover, copy the
tape file to foo.tar.gz and:

gzrecover foo.tar.gz
cpio -F foo.tar.recovered -i -v

No guarantees, but I hope this helps you as much as it helped me!

KNOWN ISSUES

gzrecover sometimes segfaults on certain files. Neither I nor anyone
else has been able to track down the source of this. I am looking for files
of reasonable size for which I can replicate this bug on Linux, so if
you encounter it with a file that isn't huge, let me know.

COPYRIGHT NOTICE 

gzrecover written by Aaron M. Renn (arenn@urbanophile.com)
Copyright (c) 2002-2013 Aaron M. Renn. 

This code is licensed under the same GNU General Public License v2
(or at your option, any later version).  See
http://www.gnu.org/licenses/gpl.html

