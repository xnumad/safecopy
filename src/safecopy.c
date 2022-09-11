/**
 * This file is copyright ©2012 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
//sorry for this file being one big long unreadable mess
//the main operation loop actually sits in main()
//basically processing the following sequence
// 1.declarations
// 2.command line parsing
// 3.opening io handles
// 4.initialisations
// 5.dynamic initialisations and tests
// 6.main io loop
// 6.a planning - calculate wanted read position based on include/exclude input files
// 6.b navigation - attempt to seek to requested input file position and find out actual position
// 6.c patience - wait for availability of data
// 6.d input - attempt to read from sourcefile
// 6.e feedback - calculate and display user feedback information
// 6.f reaction - act according to result of read operation
// 6.f.1 successful read:
// 6.f.1.a attempt to backtrack for readable data prior to current position or...
// 6.f.1.b write to output data file
// 6.f.2 failed read
// 6.f.2.a try again or...
// 6.f.2.b skip over bad area
// 6.f.2.c close and reopen source file
// 7.closing and finalisation

#define _FILE_OFFSET_BITS 64
// make off_t a 64 bit pointer on system that support it
#define _GNU_SOURCE
// allow usage of O_DIRECT if available

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "arglist.h"
#include "lowlevel.h"

// define syncmode - use O_DIRECT if supported
#ifdef O_DIRECT
	#define O_SAFECOPYSYNC O_DIRECT
#else
	#define O_SAFECOPYSYNC O_RSYNC
#endif


#define DEBUG_FLOW 1
#define DEBUG_IO 2
#define DEBUG_BADBLOCKS 4
#define DEBUG_SEEK 8
#define DEBUG_INCREMENTAL 16
#define DEBUG_EXCLUDE 32

#define MARK_INCLUDING 1
#define MARK_EXCLUDING 2

#define FAILSTRING "BaDbLoCk"
#define MAXBLOCKSIZE 104857600
#define BLOCKSIZE 4096
#define RETRIES 3
#define SEEKS 1

#define DEFBLOCKSIZE "1*"
#define DEFFAULTBLOCKSIZE "16*"
#define DEFRESOLUTION "1*"
#define DEFRETRIES 3
#define DEFHEADMOVE 1
#define DEFLOWLEVEL 1
#define DEFFAILSTRING NULL
#define DEFOUTPUTBB NULL
#define DEFINPUTBB NULL
#define DEFEXCLBB NULL
#define DEFTIMINGFILESTRING NULL

#define VERY_FAST 100
#define FAST VERY_FAST*100
#define SLOW FAST*10
#define VERY_SLOW SLOW*10
#define VERY_VERY_SLOW VERY_SLOW*10

#define VERY_FAST_ICON "  ;-}"
#define FAST_ICON "  :-)"
#define SLOW_ICON "  :-|"
#define VERY_SLOW_ICON "  8-("
#define VERY_VERY_SLOW_ICON "  8-X"

struct config_struct {
	// filenames
	char *sourcefile, *destfile, *bblocksinfile, *xblocksinfile, *bblocksoutfile, *seekscriptfile;
	// default options
	char *blocksizestring;
	char *resolutionstring;
	char *faultblocksizestring;
	char *bblocksinstring;
	char *bblocksoutstring;
	char *xblocksinstring;
	char *failuredefstring;
	char *timingfilestring;
	int retriesdef;
	int headmovedef;
	int lowleveldef;
	// indicator wether stdin/stderr is a terminal - affects output
	int human;
	// pointer to marker string
	char *marker;
};

struct status_struct {
	// file descriptors
	int source, destination, bblocksout;
	// high level file descriptor
	FILE *bblocksin, *xblocksin, *timingfile;

	// file offset variables
	off_t readposition, cposition, sposition, writeposition;
	off_t startoffset, length;
	// variables for handling read/written sizes/remainders
	off_t remain, maxremain, block, writeblock, writeremain;
	// pointer to main IO data buffer
	char * databuffer, *databufferpool;
	// a buffer for output text
	char textbuffer[256];
	// several local integer variables
	off_t fsblocksize, blocksize, iblocksize, xblocksize, faultblocksize;
	off_t resolution;
	int retries, seeks, cseeks;
	int incremental, excluding, lowlevel, syncmode, forceopen;
	int counter, percent, oldpercent, newerror, newsofterror;
	int backtracemode, output, linewidth, seekable, desperate;

	// error indicators and flags
	off_t softerr, harderr, lasterror, lastgood, lastmarked;
	// variables to remember beginning and end of previous good/bad area
	off_t lastbadblock, lastxblock, previousxblock, lastsourceblock;
	// input filesize and size of unreadable area
	off_t filesize, damagesize, targetsize;
	// times
	struct timeval oldtime, newtime;
	// and timing helper variables
	long int elapsed, oldelapsed, oldcategory;

};

static int debugmode = 0;

int debug(int debug, char *format,...) {
	if (debugmode & debug) {
		va_list ap;
		int ret;
		va_start(ap, format);
		ret = vfprintf(stderr, format, ap);
		va_end(ap);
		return ret;
	}
	return 0;
}

void usage(char * name, FILE* printout) {
	fprintf(printout, "Safecopy "VERSION" by CorvusCorax\n");
	fprintf(printout, "Usage: %s [options] <source> <target>\n", name);
	fprintf(printout, "Options:\n");
	fprintf(printout, "	--stage1 : Preset to rescue most of the data fast,\n");
	fprintf(printout, "	           using no retries and avoiding bad areas.\n");
	fprintf(printout, "	           Presets: -f 10%% -r 10%% -R 1 -Z 0 -L 2 -M %s\n", FAILSTRING);
	fprintf(printout, "	                    -o stage1.badblocks\n");
	fprintf(printout, "	--stage2 : Preset to rescue more data, using no retries\n");
	fprintf(printout, "	           but searching for exact ends of bad areas.\n");
	fprintf(printout, "	           Presets: -f 128* -r 1* -R 1 -Z 0 -L 2\n");
	fprintf(printout, "	                    -I stage1.badblocks\n");
	fprintf(printout, "	                    -o stage2.badblocks\n");
	fprintf(printout, "	--stage3 : Preset to rescue everything that can be rescued\n");
	fprintf(printout, "	           using maximum retries, head realignment tricks\n");
	fprintf(printout, "	           and low level access.\n");
	fprintf(printout, "	           Presets: -f 1* -r 1* -R 4 -Z 1 -L 2\n");
	fprintf(printout, "	                    -I stage2.badblocks\n");
	fprintf(printout, "	                    -o stage3.badblocks\n");
	fprintf(printout, "	All stage presets can be overridden by individual options.\n");
	fprintf(printout, "	-b <size> : Blocksize for default read operations.\n");
	fprintf(printout, "	            Set this to the physical sectorsize of your media.\n");
	fprintf(printout, "	            Default: 1*\n");
	fprintf(printout, "	            Hardware block size if reported by OS, otherwise %i\n", BLOCKSIZE);
	fprintf(printout, "	-f <size> : Blocksize when skipping over badblocks.\n");
	fprintf(printout, "	            Higher settings put less strain on your hardware,\n");
	fprintf(printout, "	            but you might miss good areas in between two bad ones.\n");
	fprintf(printout, "	            Default: 16*\n");
	fprintf(printout, "	-r <size> : Resolution in bytes when searching for the exact\n");
	fprintf(printout, "	            beginning or end of a bad area.\n");
	fprintf(printout, "	            If you read data directly from a device there is no\n");
	fprintf(printout, "	            need to set this lower than the hardware blocksize.\n");
	fprintf(printout, "	            On mounted filesystems however, read blocks\n");
	fprintf(printout, "	            and physical blocks could be misaligned.\n");
	fprintf(printout, "	            Smaller values lead to very thorough attempts to read\n");
	fprintf(printout, "	            data at the edge of damaged areas,\n");
	fprintf(printout, "	            but increase the strain on the damaged media.\n");
	fprintf(printout, "	            Default: 1*\n");
	fprintf(printout, "	-R <number> : At least that many read attempts are made on the first\n");
	fprintf(printout, "	              bad block of a damaged area with minimum resolution.\n");
	fprintf(printout, "	              More retries can sometimes recover a weak sector,\n");
	fprintf(printout, "	              but at the cost of additional strain.\n");
	fprintf(printout, "	              Default: %i\n", RETRIES);
	fprintf(printout, "	-Z <number> : On each error, force seek the read head from start to\n");
	fprintf(printout, "	              end of the source device as often as specified.\n");
	fprintf(printout, "	              That takes time, creates additional strain and might\n");
	fprintf(printout, "	              not be supported by all devices or drivers.\n");
	fprintf(printout, "	              Default: %i\n", SEEKS);
	fprintf(printout, "	-L <mode> : Use low level device calls as specified:\n");
	fprintf(printout, "	                   0  Do not use low level device calls\n");
	fprintf(printout, "	                   1  Attempt low level device calls\n");
	fprintf(printout, "	                      for error recovery only\n");
	fprintf(printout, "	                   2  Always use low level device calls\n");
	fprintf(printout, "	                      if available\n");
	fprintf(printout, "	            Supported low level features in this version are:\n");
	fprintf(printout, "	                SYSTEM  DEVICE TYPE   FEATURE\n");
	fprintf(printout, "	                Linux   cdrom/dvd     bus/device reset\n");
	fprintf(printout, "	                Linux   cdrom         read sector in raw mode\n");
	fprintf(printout, "	                Linux   floppy        controller reset, twaddle\n");
	fprintf(printout, "	            Default: %i\n", DEFLOWLEVEL);
	fprintf(printout, "	--sync : Use synchronized read calls (disable driver buffering).\n");
	fprintf(printout, "	         Safecopy will use O_DIRECT if supported by the OS\n");
	fprintf(printout, "	         and O_SYNC otherwise.\n");
	fprintf(printout, "	         Default: Asynchronous read buffering by the OS is allowed\n");
	fprintf(printout, "	--forceopen : Keep trying to reopen the source after a read errer\n");
	fprintf(printout, "	              useful for USB drives that go away temporarily.\n");
	fprintf(printout, "	              Warning: This can cause safecopy to hang\n");
	fprintf(printout, "	                       until aborted manually!\n");
	fprintf(printout, "	              Default: Abort on fopen() error\n");
	fprintf(printout, "	-s <blocks> : Start position where to start reading.\n");
	fprintf(printout, "	              Will correspond to position 0 in the destination file.\n");
	fprintf(printout, "	              Default: block 0\n");
	fprintf(printout, "	-l <blocks> : Maximum length of data to be read.\n");
	fprintf(printout, "	              Default: Entire size of input file\n");
	fprintf(printout, "	-I <badblockfile> : Incremental mode. Assume the target file already\n");
	fprintf(printout, "	                    exists and has holes specified in the badblockfile.\n");
	fprintf(printout, "	                    It will be attempted to retrieve more data from\n");
	fprintf(printout, "	                    the listed blocks or from beyond the file size\n");
	fprintf(printout, "	                    of the target file only.\n");
	fprintf(printout, "	                    Warning: Without this option, the destination file\n");
	fprintf(printout, "	                    will be emptied prior to writing.\n");
	fprintf(printout, "	                    Use -I /dev/null if you want to continue a previous\n");
	fprintf(printout, "	                    run of safecopy without a badblock list.\n");
	fprintf(printout, "	                    Implies: -c 0 if -c is not specified\n");
	fprintf(printout, "	                    Default: none ( /dev/null if -c is given )\n");
	fprintf(printout, "	-i <bytes> : Blocksize to interpret the badblockfile given with -I.\n");
	fprintf(printout, "	             Default: Blocksize as specified by -b\n");
	fprintf(printout, "	-c <blocks> : Continue copying at this position.\n");
	fprintf(printout, "	              This allows continuing if the output is a block device\n");
	fprintf(printout, "	              with a fixed size as opposed to a growable file,\n");
	fprintf(printout, "	              where safecopy cannot determine how far it already got.\n");
	fprintf(printout, "	              The blocksize used is the same as for the -I option.\n");
	fprintf(printout, "	              -c 0 will continue at the current destination size.\n");
	fprintf(printout, "	              Implies: -I /dev/null if -I is not specified\n");
	fprintf(printout, "	              Default: none, 0 if \-I is specified\n");
	fprintf(printout, "	-X <badblockfile> : Exclusion mode. If used together with -I,\n");
	fprintf(printout, "	                    excluded blocks override included blocks.\n");
	fprintf(printout, "	                    Safecopy will not read or write any data from\n");
	fprintf(printout, "	                    areas covered by exclude blocks.\n");
	fprintf(printout, "	                    Default: none ( 0 if -I is given ) \n");
	fprintf(printout, "	-x <bytes> : Blocksize to interpret the badblockfile given with -X.\n");
	fprintf(printout, "	             Default: Blocksize as specified by -b\n");
	fprintf(printout, "	-o <badblockfile> : Write a badblocks/e2fsck compatible bad block file.\n");
	fprintf(printout, "	                    Default: none\n");
	fprintf(printout, "	-S <seekscript> : Use external script for seeking in input file.\n");
	fprintf(printout, "	                  (Might be useful for tape devices and similar).\n");
	fprintf(printout, "	                  Seekscript must be an executable that takes the\n");
	fprintf(printout, "	                  number of blocks to be skipped as argv1 (1-64)\n");
	fprintf(printout, "	                  the blocksize in bytes as argv2\n");
	fprintf(printout, "	                  and the current position (in bytes) as argv3.\n");
	fprintf(printout, "	                  Return value needs to be the number of blocks\n");
	fprintf(printout, "	                  successfully skipped, or 0 to indicate seek failure.\n");
	fprintf(printout, "	                  The external seekscript will only be used\n");
	fprintf(printout, "	                  if lseek() fails and we need to skip over data.\n");
	fprintf(printout, "	                  Default: none\n");
	fprintf(printout, "	-M <string> : Mark unrecovered data with this string instead of\n");
	fprintf(printout, "	              skipping it. This helps in later finding corrupted\n");
	fprintf(printout, "	              files on rescued file system images.\n");
	fprintf(printout, "	              The default is to zero unreadable data on creation\n");
	fprintf(printout, "	              of output files, and leaving the data as it is\n");
	fprintf(printout, "	              on any later run.\n");
	fprintf(printout, "	              Warning: When used in combination with\n");
	fprintf(printout, "	              incremental mode (-I) this may overwrite data\n");
	fprintf(printout, "	              in any block that occurs in the -I file.\n");
	fprintf(printout, "	              Blocks not in the -I file, or covered by the file\n");
	fprintf(printout, "	              specified with -X are save from being overwritten.\n");
	fprintf(printout, "	              Default: none\n");
	fprintf(printout, "	--debug <level> : Enable debug output. Level is a bit field,\n");
	fprintf(printout, "	                  add values together for more information:\n");
	fprintf(printout, "	                     program flow:     %i\n", DEBUG_FLOW);
	fprintf(printout, "	                     IO control:       %i\n", DEBUG_IO);
	fprintf(printout, "	                     badblock marking: %i\n", DEBUG_BADBLOCKS);
	fprintf(printout, "	                     seeking:          %i\n", DEBUG_SEEK);
	fprintf(printout, "	                     incremental mode: %i\n", DEBUG_INCREMENTAL);
	fprintf(printout, "	                     exclude mode:     %i\n", DEBUG_EXCLUDE);
	fprintf(printout, "	                  or for all debug output: %i\n", 255);
	fprintf(printout, "	                  Default: 0\n");
	fprintf(printout, "	-T <timingfile> : Write sector read timing information into\n");
	fprintf(printout, "	                  this file for later analysis.\n");
	fprintf(printout, "	                  Default: none\n");
	fprintf(printout, "	-h | --help : Show this text\n\n");
	fprintf(printout, "Valid parameters for -f -r -b <size> options are:\n");
	fprintf(printout, "	<integer>	Amount in bytes - i.e. 1024\n");
	fprintf(printout, "	<percentage>%%	Percentage of whole file/device size - e.g. 10%%\n");
	fprintf(printout, "	<number>*	-b only, number times blocksize reported by OS\n");
	fprintf(printout, "	<number>*	-f and -r only, number times the value of -b\n\n");
	fprintf(printout, "Description of output:\n");
	fprintf(printout, "	. : Between 1 and 1024 blocks successfully read.\n");
	fprintf(printout, "	_ : Read of block was incomplete. (possibly end of file)\n");
	fprintf(printout, "	    The blocksize is now reduced to read the rest.\n");
	fprintf(printout, "	|/| : Seek failed, source can only be read sequentially.\n");
	fprintf(printout, "	> : Read failed, reducing blocksize to read partial data.\n");
	fprintf(printout, "	! : A low level error on read attempt of smallest allowed size\n");
	fprintf(printout, "	    leads to a retry attempt.\n");
	fprintf(printout, "	[xx](+yy){ : Current block and number of bytes continuously\n");
	fprintf(printout, "	             read successfully up to this point.\n");
	fprintf(printout, "	X : Read failed on a block with minimum blocksize and is skipped.\n");
	fprintf(printout, "	    Unrecoverable error, destination file is padded with zeros.\n");
	fprintf(printout, "	    Data is now skipped until end of the unreadable area is reached.\n");
	fprintf(printout, "	< : Successful read after the end of a bad area causes\n");
	fprintf(printout, "	    backtracking with smaller blocksizes to search for the first\n");
	fprintf(printout, "	    readable data.\n");
	fprintf(printout, "	}[xx](+yy) : current block and number of bytes of recent\n");
	fprintf(printout, "	             continuous unreadable data.\n\n");
	fprintf(printout, "Copyright 2012 CorvusCorax\n");
	fprintf(printout, "This is free software. You may redistribute copies of it under\n");
	fprintf(printout, "the terms of the GNU General Public License version 2 or above.\n");
	fprintf(printout, "	<http://www.gnu.org/licenses/gpl2.html>.\n");
	fprintf(printout, "There is NO WARRANTY, to the extent permitted by law.\n");

}

// parse an option string
off_t parseoption(char* option, int blocksize , off_t filesize , char* defaultvalue) {
	if (option == NULL) {
		return parseoption(defaultvalue, blocksize , filesize , defaultvalue);
	}
	int len = strlen(option);
	int number;
	off_t result;
	char* newoption = strdup(option);
	if (arglist_isinteger(option) == 0) {
		return (arglist_integer(option));
	}
	if (len < 2) return parseoption(defaultvalue, blocksize , filesize , defaultvalue);
	if (option[len-1] == '%') {
		newoption[len-1]=0;
		if (arglist_isinteger(newoption) == 0) {
			number = arglist_integer(newoption);
			if ( filesize  > 0) {
				result = (( filesize *number)/100);
			} else {
				result = ( blocksize *number);
			}
			// round by blocksize
			return ((((result/ blocksize ) > 0)?(result/ blocksize ):1)* blocksize );
		}
		return parseoption(defaultvalue, blocksize , filesize , defaultvalue);
	}
	if (option[len-1] == '*') {
		newoption[len-1]=0;
		if (arglist_isinteger(newoption) == 0) {
			number = arglist_integer(newoption);
			return ( blocksize *number);
		}
		return parseoption(defaultvalue, blocksize , filesize , defaultvalue);
	}
	return parseoption(defaultvalue, blocksize , filesize , defaultvalue);
}

// print percentage to stderr
void printpercentage(int percent ) {
	char percentage[16]="100%";
	int t = 0;
	if ( percent > 100) percent = 100;
	if ( percent < 0) percent = 0;
	sprintf(percentage, "      %i%%", percent );
	write(2, percentage, strlen(percentage));
	while (percentage[t++] != '\x0') {
		write(2, &"\b", 1);
	}
}

// calculate difference in usecs between two struct timevals
long int timediff(struct timeval oldtime , struct timeval newtime ) {

	long int usecs = newtime .tv_usec- oldtime .tv_usec;
	usecs = usecs+(( newtime .tv_sec- oldtime .tv_sec)*1000000);
	if (usecs < 0) usecs = 0;
	return usecs;

}

// map delays to quality categories
int timecategory(long int time) {
	if (time <= VERY_FAST) return VERY_FAST;
	if (time <= FAST) return FAST;
	if (time <= SLOW) return SLOW;
	if (time <= VERY_SLOW) return VERY_SLOW;
	return VERY_VERY_SLOW;
}

// map quality categories to icons
char* timeicon(int timecat) {
	switch (timecat) {
		case VERY_VERY_SLOW: return VERY_VERY_SLOW_ICON;
		case VERY_SLOW: return VERY_SLOW_ICON;
		case SLOW: return SLOW_ICON;
		case FAST: return FAST_ICON;
		case VERY_FAST: return VERY_FAST_ICON;
	}
	return "  ???";
}

// print quality indicator to stderr
void printtimecategory(int timecat) {
	char * icon = timeicon(timecat);
	int t = 0;
	write(2, icon, strlen(icon));
	while (icon[t++] != '\x0') {
		write(2, &"\b", 1);
	}
}

// global flag variable and handler for CTRL+C
volatile int wantabort = 0;
void signalhandler(int sig) {
	wantabort = 1;
}

// wrapper for external script to seek in character device files (tapes)
off_t emergency_seek(off_t new, off_t old, off_t blocksize , char* script) {
	char firstarg[128];
	char secondarg[128];
	char thirdarg[128];
	off_t delta = new-old;
	int status;
	debug(DEBUG_SEEK, "debug: emergency seek");
	// do nothing if no seek is necessary
	if (new == old) return old;
	// do nothing if no script is given
	if (script == NULL) return (-2);
	// because of the limited size of EXITSTATUS,
	// we need to call separate seeks for big skips
	while (delta > ( blocksize *64)) {
		old = emergency_seek(old+( blocksize *64), old, blocksize , script);
		if (old < 0) return old;
		delta = new-old;
	}
	// minimum seek is one block
	if (delta < blocksize ) delta = blocksize ;
	// call a script
	sprintf(firstarg, "%llu", (long long)(delta/ blocksize ));
	sprintf(secondarg, "%llu", (long long)blocksize );
	sprintf(thirdarg, "%llu", (long long)old);
	pid_t child = fork();
	if (child == 0) {
		execlp(script, script, firstarg, secondarg, thirdarg, NULL);
		exit(-2);
	} else if ( child < 0) {
		return (-2);
	}
	waitpid(child, &status, 0);
	// return exit code - calculate bytes from blocks in case of positive exit value
	if (WEXITSTATUS(status) == 0) return (-2);
	return (old+( blocksize *WEXITSTATUS(status)));
}

// function to mark bad blocks in output
void markbadblocks(int destination , off_t writeposition , off_t remain , char* marker, char* databuffer , off_t blocksize )
{
	off_t writeoffset, writeremain , writeblock , cposition ;
	char nullmarker[8]={0};
	if ( remain <= 0) {
		debug(DEBUG_BADBLOCKS, "debug: no bad blocks to mark\n");
		return;
	}
	debug(DEBUG_BADBLOCKS, "debug: marking %llu bad bytes at %llu\n", remain , writeposition );
	// if a marker is given, we need to write it to the
	// destination at the current position
	// first copy the marker into the data buffer
	writeoffset = 0;
	writeremain = strlen(marker);
	if ( writeremain == 0) writeremain = 8;
	while (writeoffset+ writeremain < blocksize ) {
		if ( writeremain != 0) {
			memcpy( databuffer +writeoffset, marker, writeremain );
		} else {
			memcpy( databuffer +writeoffset, nullmarker, writeremain );
		}
		writeoffset += writeremain ;
	}
	memcpy( databuffer + writeoffset, marker, blocksize -writeoffset);
	// now write it to disk
	writeremain = remain ;
	writeoffset = 0;
	while ( writeremain > 0) {
		// write data to destination file
		debug(DEBUG_SEEK, "debug: seek in destination file: %llu\n", writeposition );
		cposition = lseek( destination , writeposition , SEEK_SET);
		if ( cposition < 0) {
			fprintf(stderr, "\nError: seek() in output failed");
			perror(" ");
			wantabort = 2;
			return;
		}
		debug(DEBUG_IO, "debug: writing badblock marker to destination file: %llu bytes at %llu\n", writeremain , cposition );
		writeblock = write( destination , databuffer +(writeoffset % blocksize ),( blocksize > writeremain ? writeremain : blocksize ));
		if ( writeblock <= 0) {
			fprintf(stderr, "\nError: write to output failed");
			perror(" ");
			wantabort = 2;
			return;
		}
		writeremain -= writeblock ;
		writeoffset += writeblock ;
		writeposition += writeblock ;
	}
}

// write blocks to badblock file - up to but not including given position
void outputbadblocks(off_t start, off_t limit, int bblocksout, off_t * lastbadblock , off_t startoffset , off_t blocksize , char* textbuffer ) {
	off_t tmp_pos;
	// write badblocks to file if requested
	// start at first bad block in current bad set
	tmp_pos = (start/ blocksize );
	// override that with the first not yet written one
	if (* lastbadblock >= tmp_pos) {
		tmp_pos = * lastbadblock +1;
	}
	// then write all blocks that are smaller than the current one
	// note the calculation takes into account wether
	// the current block STARTS in a error but is ok here 
	// (which wouldnt be the case if we compared tmp_pos directly)
	while ((tmp_pos* blocksize ) < limit) {
		* lastbadblock = tmp_pos;
		sprintf( textbuffer , "%llu\n", (long long)* lastbadblock );
		debug(DEBUG_BADBLOCKS, "debug: declaring bad block: %llu\n", * lastbadblock );
		write(bblocksout, textbuffer , strlen( textbuffer ));
		tmp_pos++;
	}
}

// function to mark a given section in both destination file (badblock marking) and badblock output
void realmarkoutput(
	// these are relevant
	off_t start, off_t end, off_t min, off_t max,
	struct config_struct *configvars, struct status_struct *statusvars
) {


	off_t first = start;
	off_t last = end;
	char * tmp;
	off_t tmp_pos;
	if (min+ statusvars->startoffset > first) first = min+ statusvars->startoffset ;
	if (max+ statusvars->startoffset < last) last = max+ statusvars->startoffset ;

	if ( statusvars->excluding ) {

		// Exclusion mode, check relevant exclude sectors whether they affect the current position
		// first check if we need to backtrack in exclude file.
		debug(DEBUG_EXCLUDE, "debug: checking for exclude blocks during output, at position %llu\n", first);
		if (first <  statusvars->lastxblock * statusvars->xblocksize ) {
			debug(DEBUG_EXCLUDE, "debug: possibly need backtracking in exclude list, next exclude block %lli\n", statusvars->lastxblock );
			if (first < statusvars->previousxblock * statusvars->xblocksize ) {
				// we read too far in exclude block file, probably after backtracking
				// close exclude file and reopen
				debug(DEBUG_EXCLUDE, "debug: reopening exclude file and reading from the start\n");
				fclose(statusvars->xblocksin);
				statusvars->xblocksin = fopen(configvars->xblocksinfile, "r");
				if (statusvars->xblocksin == NULL) {
					fprintf(stderr, "Error reopening exclusion badblock file for reading: %s", configvars->xblocksinfile);
					perror(" ");
					wantabort = 2;
					statusvars->previousxblock = ((unsigned)-1)>>1;
					statusvars->lastxblock = ((unsigned)-1)>>1;
					return;
				}
				statusvars->lastxblock = -1;
				statusvars->previousxblock = -1;
			} else if ((statusvars->previousxblock +1) * statusvars->xblocksize > first) {
				// backtrack just one exclude block
				statusvars->lastxblock = statusvars->previousxblock ;
				debug(DEBUG_EXCLUDE, "debug: using last exclude block %lli\n", statusvars->lastxblock );
			} else {
				debug(DEBUG_EXCLUDE, "debug: false alarm, current exclude block is fine\n");
			}
		}
		tmp_pos = statusvars->lastxblock * statusvars->xblocksize ;
		while (tmp_pos < last) {
			if (tmp_pos+ statusvars->xblocksize > first) {
				if (tmp_pos <= first) {
					// start of current block is covered by exclude block. skip to end and try again
					first = tmp_pos+ statusvars->xblocksize ;
					if (first > last) {
						debug(DEBUG_EXCLUDE, "debug: current bad block area is completely covered by xblocks, skipping\n");
						return;
					}
					debug(DEBUG_EXCLUDE, "debug: start of current bad block area is covered by xblocks shrinking\n");
					realmarkoutput(first, last, first- statusvars->startoffset , last- statusvars->startoffset , configvars, statusvars);
					
					return;
				} else if (tmp_pos < last) {
					// 
// ATTENTION: there could be a reamaining part behind this xblock - needs two recursive calls to self to fix!
					debug(DEBUG_EXCLUDE, "debug: current bad block area is partially covered by xblocks, splitting\n");
					realmarkoutput(first, tmp_pos, first- statusvars->startoffset , tmp_pos- statusvars->startoffset , configvars, statusvars);
					if (wantabort>1) return;
					realmarkoutput(tmp_pos, last, tmp_pos- statusvars->startoffset , last- statusvars->startoffset , configvars, statusvars);
					return;
				} else {
					// start of exclude block is beyond end of our area. we are done
					break;
				}
			} else {
				// read next exclude block
				tmp = fgets( statusvars->textbuffer , 64, statusvars->xblocksin);
				long long unsigned int dummy;
				if (sscanf( statusvars->textbuffer , "%llu", &dummy) != 1) tmp = NULL;
				if ( tmp!=NULL && (signed)dummy < (signed)statusvars->lastxblock ) {
					fprintf(stderr, "Parse error in badblocks file %s: not sorted correctly!\n", configvars->xblocksinfile);
					wantabort = 2;
					return;
				}
				tmp_pos = dummy;
				if (tmp == NULL) {
					// no more bad blocks in input file
					break;
				}
				statusvars->previousxblock = statusvars->lastxblock ;
				statusvars->lastxblock = tmp_pos;
				tmp_pos = statusvars->lastxblock * statusvars->xblocksize ;
			}
		}
	}

	if (configvars->marker) {
		debug(DEBUG_BADBLOCKS, "debug: marking badblocks from %llu to %llu \n", first, last);
		if (statusvars->lastmarked < first- statusvars->startoffset ) {
			statusvars->lastmarked = first- statusvars->startoffset ;
		}
		markbadblocks(statusvars->destination, statusvars->lastmarked , last-(statusvars->lastmarked + statusvars->startoffset ), configvars->marker, statusvars->databuffer , statusvars->blocksize );
	}
	if (configvars->bblocksoutfile != NULL) {
		debug(DEBUG_BADBLOCKS, "debug: declaring badblocks from %llu to %llu \n", first, last);

		outputbadblocks(first, last, statusvars->bblocksout, &statusvars->lastbadblock , statusvars->startoffset , statusvars->blocksize , statusvars->textbuffer );
	}
}

// function to mark output - taking include file information into account to not touch not mentioned blocks
void markoutput(
	// these are relevant
char* description, off_t readposition , off_t lastgood ,
	struct config_struct *configvars, struct status_struct *statusvars
) {
	off_t tmp_pos;
	char *tmp;

	tmp_pos = lastgood + statusvars->startoffset ;
	// check for incremental mode in incremental mode, handle only sectors mentioned in include file
	// for marking, the include sector alignments are relevant, for badblock output the affected blocks
	// in our blocksize
	if ( statusvars->incremental ) {
		off_t inc_pos = statusvars->lastsourceblock * statusvars->iblocksize ;
		// repeat as long as 
		while (inc_pos < readposition + statusvars->startoffset ) {
			if (inc_pos+ statusvars->iblocksize > tmp_pos) {
				debug(DEBUG_BADBLOCKS, "debug: %s %llu - %llu - marking output for infile block %lli (%llu - %llu)\n", description, tmp_pos, readposition + statusvars->startoffset , inc_pos/ statusvars->iblocksize , inc_pos, inc_pos+ statusvars->iblocksize );
				realmarkoutput(inc_pos, inc_pos+ statusvars->iblocksize , lastgood , readposition , configvars, statusvars);
				if (wantabort>1) return;
			}
			if (inc_pos+ statusvars->iblocksize > readposition + statusvars->startoffset ) {
				// do not read in another include block
				// if the current one is still good for future use.
				break;
			}
			tmp = fgets( statusvars->textbuffer , 64, statusvars->bblocksin);
			long long unsigned int dummy;
			if (sscanf( statusvars->textbuffer , "%llu", &dummy ) != 1) tmp = NULL;
			if ( tmp!=NULL && (signed)dummy < (signed)statusvars->lastsourceblock ) {
				fprintf(stderr, "Parse error in badblocks file %s: not sorted correctly!\n", configvars->bblocksinfile);
				wantabort = 2;
				return;
			}
			statusvars->lastsourceblock = dummy;
			if (tmp == NULL) {
				// no more bad blocks in input file
				// if exists
				if (( readposition + statusvars->startoffset ) < statusvars->targetsize ) {
					// go to end of target file for resuming
					statusvars->lastsourceblock = statusvars->targetsize / statusvars->iblocksize ;
				} else if ( statusvars->targetsize ) {
					statusvars->lastsourceblock = ( readposition + statusvars->startoffset )/ statusvars->iblocksize ;
				} else {
					break;
				}
			}
			inc_pos = statusvars->lastsourceblock * statusvars->iblocksize ;
		}
	} else {
		debug(DEBUG_BADBLOCKS, "debug: %s %llu - %llu - marking output for whole bad area\n", description, tmp_pos, readposition + statusvars->startoffset );
		realmarkoutput(tmp_pos, readposition + statusvars->startoffset , lastgood , readposition , configvars, statusvars);
	}

}

void select_for_reading(int source , struct config_struct *configvars, struct status_struct *statusvars) {
	struct timeval mytime;
	fd_set rfds, efds;

	// select for reading. Have a fallback output in case of timeout.
	do {
		mytime.tv_sec = 10;
		mytime.tv_usec = 0;
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		FD_SET( source , &rfds);
		FD_SET( source , &efds);
		select( source +1, &rfds, NULL, &efds, &mytime);
		if (! ( FD_ISSET( source , &rfds))) {
			statusvars->desperate = 1;
			if (configvars->human) {
				if (statusvars->filesize) {
					printpercentage(statusvars->percent);
				}
				printtimecategory(VERY_VERY_SLOW);
			}
		}
		if (wantabort) break;
	} while (! ( FD_ISSET( source , &rfds) || FD_ISSET( source , &efds)));
}

// main
int main(int argc, char ** argv) {

// 1.declarations
	// commandline argument handler class
	struct arglist *carglist;

	// configuration
	struct config_struct global_configuration;
	global_configuration.blocksizestring = DEFBLOCKSIZE;
	global_configuration.resolutionstring = DEFRESOLUTION;
	global_configuration.faultblocksizestring = DEFFAULTBLOCKSIZE;
	global_configuration.bblocksinstring = DEFINPUTBB;
	global_configuration.bblocksoutstring = DEFOUTPUTBB;
	global_configuration.xblocksinstring = DEFEXCLBB;
	global_configuration.failuredefstring = DEFFAILSTRING;
	global_configuration.timingfilestring = DEFTIMINGFILESTRING; 
	global_configuration.retriesdef = DEFRETRIES;
	global_configuration.headmovedef = DEFHEADMOVE;
	global_configuration.lowleveldef = DEFLOWLEVEL;
	global_configuration.human = 0;
	global_configuration.marker = NULL;
	struct config_struct *configvars = &global_configuration; 
	
	// status
	struct status_struct global_status;
	struct status_struct *statusvars = &global_status;

	// buffer pointer for sfgets() 
	char *tmp;
	// tmp vars for file offsets
	off_t tmp_pos, tmp_bytes;
	// stat() needs this
	struct stat filestatus;
	// tmp int vars
	int errtmp;

// 2.command line parsing

	// parse all commandline arguments
	carglist = arglist_new(argc, argv);
	arglist_addarg(carglist, "--stage", 1);
	arglist_addarg(carglist, "--stage1", 0);
	arglist_addarg(carglist, "--stage2", 0);
	arglist_addarg(carglist, "--stage3", 0);
	arglist_addarg(carglist, "--debug", 1);
	arglist_addarg(carglist, "--help", 0);
	arglist_addarg(carglist, "-h", 0);
	arglist_addarg(carglist, "--sync", 0);
	arglist_addarg(carglist, "--forceopen", 0);
	arglist_addarg(carglist, "-b", 1);
	arglist_addarg(carglist, "-f", 1);
	arglist_addarg(carglist, "-r", 1);
	arglist_addarg(carglist, "-R", 1);
	arglist_addarg(carglist, "-s", 1);
	arglist_addarg(carglist, "-L", 1);
	arglist_addarg(carglist, "-l", 1);
	arglist_addarg(carglist, "-o", 1);
	arglist_addarg(carglist, "-I", 1);
	arglist_addarg(carglist, "-i", 1);
	arglist_addarg(carglist, "-c", 1);
	arglist_addarg(carglist, "-X", 1);
	arglist_addarg(carglist, "-x", 1);
	arglist_addarg(carglist, "-S", 1);
	arglist_addarg(carglist, "-Z", 1);
	arglist_addarg(carglist, "-M", 1);
	arglist_addarg(carglist, "-T", 1);

	// find out whether the user is wetware
	configvars->human = (isatty(1) & isatty(2));
	
	if ((arglist_arggiven(carglist, "--debug") == 0)) {
		debugmode = arglist_integer(arglist_parameter(carglist, "--debug", 0));
	}
	if ((arglist_arggiven(carglist, "--help") == 0)
			|| (arglist_arggiven(carglist, "-h") == 0)
			|| (arglist_parameter(carglist, "VOIDARGS", 2) != NULL)
			|| (arglist_parameter(carglist, "VOIDARGS", 1) == NULL)) {
		usage(argv[0],stdout);
		arglist_kill(carglist);
		return 0;
	}
	configvars->sourcefile = arglist_parameter(carglist, "VOIDARGS", 0);
	configvars->destfile = arglist_parameter(carglist, "VOIDARGS", 1);

	if (arglist_arggiven(carglist, "--stage1") == 0 ||arglist_integer(arglist_parameter(carglist, "--stage", 0)) == 1) {
		configvars->faultblocksizestring = "10%";
		configvars->resolutionstring = "10%";
		configvars->retriesdef = 1;
		configvars->headmovedef = 0;
		configvars->lowleveldef = 2;
		configvars->failuredefstring = FAILSTRING;
		configvars->bblocksoutstring = "stage1.badblocks";
	}
	if (arglist_arggiven(carglist, "--stage2") == 0 || arglist_integer(arglist_parameter(carglist, "--stage", 0)) == 2) {
		configvars->faultblocksizestring = "128*";
		configvars->resolutionstring = "1*";
		configvars->retriesdef = 1;
		configvars->headmovedef = 0;
		configvars->lowleveldef = 2;
		configvars->bblocksinstring = "stage1.badblocks";
		configvars->bblocksoutstring = "stage2.badblocks";
	}
	if (arglist_arggiven(carglist, "--stage3") == 0 || arglist_integer(arglist_parameter(carglist, "--stage", 0)) == 3) {
		configvars->faultblocksizestring = "1*";
		configvars->resolutionstring = "1*";
		configvars->bblocksinstring = "stage2.badblocks";
		configvars->bblocksoutstring = "stage3.badblocks";
		configvars->retriesdef = 4;
		configvars->headmovedef = 1;
		configvars->lowleveldef = 2;
	}

	// low level calls enabled?
	statusvars->lowlevel = configvars->lowleveldef;
	if (arglist_arggiven(carglist, "-L") == 0) {
		statusvars->lowlevel = arglist_integer(arglist_parameter(carglist, "-L", 0));
	}
	if ( statusvars->lowlevel < 0) statusvars->lowlevel = 0;
	if ( statusvars->lowlevel > 2) statusvars->lowlevel = 2;
	fprintf(stdout, "Low level device calls enabled mode: %i\n", statusvars->lowlevel );

	// synchronous IO
	statusvars->syncmode = 0;
	if (arglist_arggiven(carglist, "--sync") == 0) {
		if (O_SAFECOPYSYNC == O_SYNC) {
			fprintf(stdout, "Using synchronized IO on source. (O_SYNC)\n");
		} else {
			fprintf(stdout, "Using synchronized IO on source. (O_DIRECT)\n");
		}
		statusvars->syncmode = O_SAFECOPYSYNC;
	}
	
	// synchronous IO
	statusvars->forceopen = 0;
	if (arglist_arggiven(carglist, "--forceopen") == 0) {
		fprintf(stdout, "Forced reopening of source file even if device is temporarily gone.\n");
		statusvars->forceopen = 1;
	}

	// find out source file size and block size
	statusvars->filesize = 0;
	statusvars->fsblocksize = BLOCKSIZE;
	if (!stat(configvars->sourcefile, &filestatus)) {
		statusvars->filesize = filestatus.st_size;
		if (filestatus.st_blksize) {
			fprintf(stdout, "Reported hw blocksize: %lu\n", filestatus.st_blksize);
			statusvars->fsblocksize = filestatus.st_blksize;
		}
	}
	if ( statusvars->lowlevel > 0) {
		statusvars->filesize = lowlevel_filesize(configvars->sourcefile, statusvars->filesize );
		statusvars->fsblocksize = lowlevel_blocksize(configvars->sourcefile, statusvars->fsblocksize );
		fprintf(stdout, "Reported low level blocksize: %lu\n", statusvars->fsblocksize );
	}

	if ( statusvars->filesize != 0) {
		fprintf(stdout, "File size: %llu\n", (long long) statusvars->filesize );
	} else {
		fprintf(stderr, "Filesize not reported by stat(), trying seek().\n");
		statusvars->source = open(configvars->sourcefile, O_RDONLY | O_RSYNC);
		if (statusvars->source) {
			statusvars->filesize = lseek(statusvars->source, 0, SEEK_END);
			close(statusvars->source);
		}
		if ( statusvars->filesize <= 0) {
			statusvars->filesize = 0;
			fprintf(stderr, "Unable to determine input file size.\n");
		} else {
			fprintf(stdout, "File size: %llu\n", (long long)statusvars->filesize );
		}
	}
	
	tmp = configvars->blocksizestring;
	if (arglist_arggiven(carglist, "-b") == 0) {
		configvars->blocksizestring = arglist_parameter(carglist, "-b", 0);
	}
	statusvars->blocksize = parseoption(configvars->blocksizestring, statusvars->fsblocksize , statusvars->filesize , tmp);
	if ( statusvars->blocksize < 1) statusvars->blocksize = statusvars->fsblocksize ;
	if ( statusvars->blocksize > MAXBLOCKSIZE) statusvars->blocksize = MAXBLOCKSIZE;
	fprintf(stdout, "Blocksize: %llu\n", (long long)statusvars->blocksize );

	tmp = configvars->faultblocksizestring;
	if (arglist_arggiven(carglist, "-f") == 0) {
		configvars->faultblocksizestring = arglist_parameter(carglist, "-f", 0);
	}
	statusvars->faultblocksize = parseoption(configvars->faultblocksizestring, statusvars->blocksize , statusvars->filesize , tmp);
	if ( statusvars->faultblocksize < statusvars->blocksize ) statusvars->faultblocksize = statusvars->blocksize ;
	if ( statusvars->faultblocksize > MAXBLOCKSIZE) statusvars->faultblocksize = MAXBLOCKSIZE;
	fprintf(stdout, "Fault skip blocksize: %llu\n", (long long)statusvars->faultblocksize );

	tmp = configvars->resolutionstring;
	if (arglist_arggiven(carglist, "-r") == 0) {
		configvars->resolutionstring = arglist_parameter(carglist, "-r", 0);
	}
	statusvars->resolution = parseoption(configvars->resolutionstring, statusvars->blocksize , statusvars->filesize , tmp);
	if ( statusvars->resolution < 1) statusvars->resolution = 1;
	if ( statusvars->resolution > statusvars->faultblocksize ) statusvars->resolution = statusvars->faultblocksize ;
	fprintf(stdout, "Resolution: %llu\n", (long long)statusvars->resolution );
	
	statusvars->retries = configvars->retriesdef;
	if (arglist_arggiven(carglist, "-R") == 0) {
		statusvars->retries = arglist_integer(arglist_parameter(carglist, "-R", 0));
	}
	if ( statusvars->retries < 1) statusvars->retries = 1;
	fprintf(stdout, "Min read attempts: %u\n", statusvars->retries );

	statusvars->seeks = configvars->headmovedef;
	if (arglist_arggiven(carglist, "-Z") == 0) {
		statusvars->seeks = arglist_integer(arglist_parameter(carglist, "-Z", 0));
	}
	if ( statusvars->seeks < 0) statusvars->seeks = 0;
	fprintf(stdout, "Head moves on read error: %i\n", statusvars->seeks );

	statusvars->iblocksize = statusvars->blocksize ;
	if (arglist_arggiven(carglist, "-i") == 0) {
		statusvars->iblocksize = arglist_integer(arglist_parameter(carglist, "-i", 0));
	}
	if ( statusvars->iblocksize < 1 || statusvars->iblocksize > MAXBLOCKSIZE) {
		fprintf(stderr, "Error: Invalid blocksize given for bad block include file!\n");
		arglist_kill(carglist);
		return 2;
	}

	statusvars->incremental = 0;
	statusvars->targetsize = 0;
	if (arglist_arggiven(carglist, "-c") == 0) {
		configvars->bblocksinstring = "/dev/null";
		if (arglist_integer(arglist_parameter(carglist, "-c", 0))>0) {
			statusvars->targetsize = arglist_integer(arglist_parameter(carglist, "-c", 0)) * statusvars->iblocksize;
		}
	}
	if (arglist_arggiven(carglist, "-I") == 0) {
		configvars->bblocksinstring = arglist_parameter(carglist, "-I", 0);
	}
	if (configvars->bblocksinstring != NULL) {
		statusvars->incremental = 1;
		configvars->bblocksinfile = configvars->bblocksinstring;
		fprintf(stdout, "Incremental mode file: %s\nIncremental mode blocksize: %llu\n", configvars->bblocksinfile, (long long)statusvars->iblocksize );
	}

	statusvars->xblocksize = statusvars->blocksize ;
	if (arglist_arggiven(carglist, "-x") == 0) {
		statusvars->xblocksize = arglist_integer(arglist_parameter(carglist, "-x", 0));
	}
	if ( statusvars->xblocksize < 1 || statusvars->xblocksize > MAXBLOCKSIZE) {
		fprintf(stderr, "Error: Invalid blocksize given for bad block exclude file!\n");
		arglist_kill(carglist);
		return 2;
	}

	statusvars->excluding = 0;
	if (arglist_arggiven(carglist, "-X") == 0) {
		configvars->xblocksinstring = arglist_parameter(carglist, "-X", 0);
	}
	if (configvars->xblocksinstring != NULL) {
		statusvars->excluding = 1;
		configvars->xblocksinfile = configvars->xblocksinstring;
		fprintf(stdout, "Exclusion mode file: %s\nExclusion mode blocksize: %llu\n", configvars->xblocksinfile, (long long)statusvars->xblocksize );
	}

	configvars->bblocksoutfile = NULL;
	if (arglist_arggiven(carglist, "-o") == 0) {
		configvars->bblocksoutstring = arglist_parameter(carglist, "-o", 0);
	}
	if (configvars->bblocksoutstring != NULL) {
		configvars->bblocksoutfile = configvars->bblocksoutstring;
		fprintf(stdout, "Badblocks output: %s\n", configvars->bblocksoutfile);
	}

	configvars->seekscriptfile = NULL;
	if (arglist_arggiven(carglist, "-S") == 0) {
		configvars->seekscriptfile = arglist_parameter(carglist, "-S", 0);
		fprintf(stdout, "Seek script (fallback): %s\n", configvars->seekscriptfile);
	}

	if (arglist_arggiven(carglist, "-T") == 0) {
		configvars->timingfilestring = arglist_parameter(carglist, "-T", 0);
		fprintf(stdout, "Write sector timing information to file: %s\n", configvars->timingfilestring);
	}

	if (arglist_arggiven(carglist, "-M") == 0) {
		configvars->failuredefstring = arglist_parameter(carglist, "-M", 0);
		if (configvars->failuredefstring == NULL) configvars->failuredefstring = "";
	}
	if (configvars->failuredefstring != NULL) {
		configvars->marker = configvars->failuredefstring;
		fprintf(stdout, "Marker string: %s\n", configvars->marker);
	}

	statusvars->startoffset = 0;
	if (arglist_arggiven(carglist, "-s") == 0) {
		statusvars->startoffset = arglist_integer(arglist_parameter(carglist, "-s", 0));
	}
	if ( statusvars->startoffset < 1) statusvars->startoffset = 0;
	fprintf(stdout, "Starting block: %llu\n", (long long)statusvars->startoffset );
	
	statusvars->length = 0;
	if (arglist_arggiven(carglist, "-l") == 0) {
		statusvars->length = arglist_integer(arglist_parameter(carglist, "-l", 0));
	}
	if ( statusvars->length < 1) statusvars->length = -1;
	if ( statusvars->length >= 0) {
		fprintf(stdout, "Size limit (blocks): %llu\n", (long long)statusvars->length );
	}
	statusvars->startoffset = statusvars->startoffset * statusvars->blocksize ;
	statusvars->length = statusvars->length * statusvars->blocksize ;
	if ( statusvars->filesize == 0 && statusvars->length > 0) {
		statusvars->filesize = statusvars->startoffset + statusvars->length ;
	}

	statusvars->databufferpool = (char*)malloc(((2* statusvars->blocksize )+1)*sizeof(char));
	if ( statusvars->databufferpool == NULL) {
		perror("MEMORY ALLOCATION ERROR!\nCOULDNT ALLOCATE MAIN BUFFER ");
		return 2;
	}
	// align databuffer to block size - needed to support O_DIRECT and /dev/raw devices
	statusvars->databuffer = statusvars->databufferpool + statusvars->blocksize -(((uintptr_t) statusvars->databufferpool )% statusvars->blocksize );

// 3.opening io handles
		
	//open files
	fprintf(stdout, "Source: %s\nDestination: %s\n", configvars->sourcefile, configvars->destfile);
	statusvars->source = open(configvars->sourcefile, O_RDONLY | O_NONBLOCK | statusvars->syncmode );
	if (statusvars->source == -1) {
		fprintf(stderr, "Error opening sourcefile: %s", configvars->sourcefile);
		perror(" ");
		if (configvars->human) usage(argv[0],stderr);
		arglist_kill(carglist);
		return 2;
	}
	if ( statusvars->excluding == 1) {
		statusvars->xblocksin = fopen(configvars->xblocksinfile, "r");
		if (statusvars->xblocksin == NULL) {
			close(statusvars->source);
			fprintf(stderr, "Error opening exclusion badblock file for reading: %s", configvars->xblocksinfile);
			perror(" ");
			arglist_kill(carglist);
			return 2;
		}
	}
	if ( statusvars->incremental == 1) {
		statusvars->bblocksin = fopen(configvars->bblocksinfile, "r");
		if (statusvars->bblocksin == NULL) {
			close(statusvars->source);
			if ( statusvars->excluding == 1) fclose(statusvars->xblocksin);
			fprintf(stderr, "Error opening badblock file for reading: %s", configvars->bblocksinfile);
			perror(" ");
			arglist_kill(carglist);
			return 2;
		}
		statusvars->destination = open(configvars->destfile, O_WRONLY, 0666 );
		if (statusvars->destination == -1) {
			close(statusvars->source);
			fclose(statusvars->bblocksin);
			if ( statusvars->excluding == 1) fclose(statusvars->xblocksin);
			fprintf(stderr, "Error opening destination: %s", configvars->destfile);
			perror(" ");
			if (configvars->human) usage(argv[0],stderr);
			arglist_kill(carglist);
			return 2;
		}
		// try to complete incomplete (aborted) safecopies by comparing file sizes
		if (! statusvars->targetsize ) {
			if (!fstat(statusvars->destination, &filestatus)) {
				statusvars->targetsize = filestatus.st_size;
			}
		}
		if (! statusvars->targetsize ) {
			fprintf(stderr, "Destination filesize not reported by stat(), trying seek().\n");
			statusvars->targetsize = lseek(statusvars->destination, 0, SEEK_END);
			if ( statusvars->targetsize < 0) statusvars->targetsize = 0;
		}
		if (! statusvars->targetsize ) {
			fprintf(stderr, "Error determining destination file size, cannot resume!");
		} else {
			fprintf(stdout, "Current destination size: %llu\n", (long long)statusvars->targetsize );
		}
	} else {
		statusvars->destination = open(configvars->destfile, O_WRONLY | O_TRUNC | O_CREAT, 0666 );
		if (statusvars->destination == -1) {
			close(statusvars->source);
			if ( statusvars->excluding == 1) fclose(statusvars->xblocksin);
			fprintf(stderr, "Error opening destination: %s", configvars->destfile);
			perror(" ");
			if (configvars->human) usage(argv[0],stderr);
			arglist_kill(carglist);
			return 2;
		}
	}
	if (configvars->bblocksoutfile != NULL) {
		// append sectors to badblocks file if in incremental mode
		if (statusvars->incremental == 1) {
			statusvars->bblocksout = open(configvars->bblocksoutfile, O_WRONLY | O_APPEND | O_CREAT, 0666);
		} else {
			statusvars->bblocksout = open(configvars->bblocksoutfile, O_WRONLY | O_TRUNC | O_CREAT, 0666);
		}
		if (statusvars->bblocksout == -1) {
			close(statusvars->source);
			close(statusvars->destination);
			if ( statusvars->incremental == 1) fclose(statusvars->bblocksin);
			if ( statusvars->excluding == 1) fclose(statusvars->xblocksin);
			fprintf(stderr, "Error opening badblock file for writing: %s", configvars->bblocksoutfile);
			perror(" ");
			arglist_kill(carglist);
			return 2;
		}
	}

	statusvars->timingfile = NULL;
	if (configvars->timingfilestring != NULL) {
		statusvars->timingfile = fopen(configvars->timingfilestring, "a");
		if (statusvars->timingfile == NULL) {
			close(statusvars->source);
			close(statusvars->destination);
			if ( statusvars->incremental == 1) fclose(statusvars->bblocksin);
			if ( statusvars->excluding == 1) fclose(statusvars->xblocksin);
			if (statusvars->bblocksout) close(statusvars->bblocksout);
			fprintf(stderr, "Error opening timingfile for writing: %s", configvars->bblocksoutfile);
			perror(" ");
			arglist_kill(carglist);
			return 2;
		}
	}

// 4.initialisations

	// setting signal handler
	signal(SIGINT, signalhandler);

	// initialise all vars
	statusvars->readposition = 0;	//current wanted reading position in sourcefile relative to startoffset
	statusvars->writeposition = 0; //current writing position in destination file
	statusvars->block = -1; //amount of bytes read from the current reading position, negative values indicate errors
	statusvars->remain = 0; //remainder if a read operation read less than blocksize bytes
	statusvars->writeremain = 0; //remainder if a write operation wrote less than blocksize bytes
	statusvars->softerr = 0; //counter for recovered read errors
	statusvars->harderr = 0; //counter for unrecoverable read errors
	statusvars->counter = 1; //counter for visual output - a dot is printed all 1024 blocks
	statusvars->newerror = statusvars->retries ; //counter for repeated read attempts on the current block - zero indicates
		//we are currently dealing with an unrecoverable error and need to find the end of the bad area
	statusvars->newsofterror = 0; //flag that indicates previous read failures on the current block
	statusvars->lasterror = 0; //address of the last encountered bad area in source file
	statusvars->lastgood = 0; //address of the last encountered successful read in source file
	statusvars->lastmarked = 0; //last known address already marked in output file
	statusvars->lastbadblock = -1; //most recently encountered block for output badblock file
	statusvars->lastxblock = -1; //most recently encountered block number from input exclude file
	statusvars->previousxblock = -1; //2nd most recently encountered block number from input exclude file
	statusvars->lastsourceblock = -1; //most recently encountered block number from input badblock file
	statusvars->damagesize = 0; //counter for size of unreadable source file data
	statusvars->backtracemode = 0; //flag that indicates safecopy is searching for the end of a bad area
	statusvars->percent = -1; //current progress status, relative to source file size
	statusvars->oldpercent = -1; //previously output percentage, needed to indicate required updates
	statusvars->oldelapsed = 0; //recent average time period for reading one block
	statusvars->oldcategory = timecategory( statusvars->oldelapsed );
		//timecategories translate raw milliseconds into human readable classes
	statusvars->elapsed = 0; //time elapsed while reading the current block
	statusvars->output = 0; //flag indicating that output (smily, percentage, ...) needs to be updated
	statusvars->linewidth = 0; //counter for x position in text output on terminal
	statusvars->sposition = 0; //actual file pointer position in sourcefile
	statusvars->seekable = 1; //flag that sourcefile allows seeking
	statusvars->desperate = 0; //flag set to indicate required low level source device access

// 5.dynamic initialisations and tests

	// attempt to seek to start position to find out wether source file is seekable
	statusvars->cposition = lseek(statusvars->source, statusvars->startoffset , SEEK_SET);
	if ( statusvars->cposition < 0) {
		perror("Warning: Input file is not seekable ");
		statusvars->seekable = 0;
		close(statusvars->source);
		statusvars->cposition = emergency_seek( statusvars->startoffset , 0, statusvars->blocksize , configvars->seekscriptfile);
		if ( statusvars->cposition >= 0) {
			statusvars->sposition = statusvars->cposition ;
		}
		statusvars->source = open(configvars->sourcefile, O_RDONLY | O_NONBLOCK | statusvars->syncmode );
		if (statusvars->source == -1) {
			perror("Error reopening sourcefile after external seek ");
			wantabort = 2;
		}
		fprintf(stdout, "|/|");
	} else {
		statusvars->sposition = statusvars->cposition ;
		statusvars->seekable = 1;
	}
	
// 6.main io loop

	fflush(stdout);
	fflush(stderr);
	// main data loop. Continue until all data has been read or CTRL+C has been pressed
	while (!wantabort && statusvars->block != 0 && ( statusvars->readposition < statusvars->length || statusvars->length < 0)) {

// 6.a planning - calculate wanted read position based on include/exclude input files

		debug(DEBUG_FLOW, "debug: start of cycle\n");
		// start with a whole new block if we finnished the old
		if ( statusvars->remain == 0) {
			debug(DEBUG_FLOW, "debug: preparing to read a new block\n");
			// if necessary, repeatedly calculate include and exclude blocks
			// (for example if we seek to a new include block, but then exclude it,
			// so we seek to the next, then exclude it, etc)
			if ( statusvars->incremental && statusvars->newerror != 0) {
				debug(DEBUG_INCREMENTAL, "debug: incremental - searching next block\n");
				// Incremental mode. Skip over unnecessary areas.
				// check wether the current block is in the badblocks list, 
				// if so, proceed as usual,
				// otherwise seek to the next badblock in input
				tmp_pos = ( statusvars->readposition + statusvars->startoffset )/ statusvars->iblocksize ;
				if (tmp_pos != statusvars->lastsourceblock ) {
					tmp = statusvars->textbuffer ;
					while (tmp != NULL && statusvars->lastsourceblock < tmp_pos ) {
						tmp = fgets( statusvars->textbuffer , 64, statusvars->bblocksin);
						long long unsigned int dummy;
						if (sscanf( statusvars->textbuffer , "%llu", &dummy ) != 1) tmp = NULL;
						if ( tmp!= NULL && (signed)dummy < (signed)statusvars->lastsourceblock ) {
							fprintf(stderr, "Parse error in badblocks file %s: not sorted correctly!\n", configvars->bblocksinfile);
							wantabort = 2;
							break;
						}
						statusvars->lastsourceblock = dummy;
					}
					if (wantabort) break;
					if (tmp == NULL) {
						// no more bad blocks in input file
						// if exists
						if (( statusvars->readposition + statusvars->startoffset ) < statusvars->targetsize ) {
							// go to end of target file for resuming
							statusvars->lastsourceblock = statusvars->targetsize / statusvars->iblocksize ;
						} else if ( statusvars->targetsize ) {
							statusvars->lastsourceblock = tmp_pos;
						} else {
							// othewise end immediately
							statusvars->remain = 0;
							break;
						}
					}
					debug(DEBUG_INCREMENTAL, "debug: incremental - target is %llu position is %llu\n", statusvars->lastsourceblock , tmp_pos);
					statusvars->readposition = ( statusvars->lastsourceblock * statusvars->iblocksize )- statusvars->startoffset ;
				} else {
					debug(DEBUG_INCREMENTAL, "debug: incremental - still in same block\n");
				}
			}
			// make sure any misalignment to block boundaries get corrected asap
			// be sure to use skipping size when in bad area
			if ( statusvars->newerror == 0) {
				statusvars->remain = ((( statusvars->readposition / statusvars->blocksize )* statusvars->blocksize )+ statusvars->faultblocksize )- statusvars->readposition ;
			} else {
				statusvars->remain = ((( statusvars->readposition / statusvars->blocksize )* statusvars->blocksize )+ statusvars->blocksize )- statusvars->readposition ;
			}
			debug(DEBUG_FLOW, "debug: prepared to read block %lli size %llu at %llu\n",( statusvars->readposition + statusvars->startoffset )/ statusvars->blocksize , statusvars->remain , statusvars->readposition + statusvars->startoffset );
		}
		if ( statusvars->excluding && statusvars->remain != 0) {
			// Exclusion mode, check relevant exclude sectors whether they affect the current position
			// first check if we need to backtrack in exclude file.
			debug(DEBUG_EXCLUDE, "debug: checking for exclude blocks at position %llu\n", statusvars->readposition + statusvars->startoffset );
			if ( statusvars->readposition + statusvars->startoffset < statusvars->lastxblock * statusvars->xblocksize ) {
				debug(DEBUG_EXCLUDE, "debug: possibly need backtracking in exclude list, next exclude block %lli\n", statusvars->lastxblock );
				if ( statusvars->readposition + statusvars->startoffset < statusvars->previousxblock * statusvars->xblocksize ) {
					// we read too far in exclude block file, probably after backtracking
					// close exclude file and reopen
					debug(DEBUG_EXCLUDE, "debug: reopening exclude file and reading from the start\n");
					fclose(statusvars->xblocksin);
					statusvars->xblocksin = fopen(configvars->xblocksinfile, "r");
					if (statusvars->xblocksin == NULL) {
						statusvars->excluding = 0;
						fprintf(stderr, "Error reopening exclusion badblock file for reading: %s", configvars->xblocksinfile);
						perror(" ");
						wantabort = 2;
						break;
					}
					statusvars->lastxblock = -1;
					statusvars->previousxblock = -1;
				} else if (( statusvars->previousxblock +1)* statusvars->xblocksize > statusvars->readposition + statusvars->startoffset ) {
					// backtrack just one exclude block
					statusvars->lastxblock = statusvars->previousxblock ;
					debug(DEBUG_EXCLUDE, "debug: using last exclude block %lli\n", statusvars->lastxblock );
				} else {
					debug(DEBUG_EXCLUDE, "debug: false alarm, current exclude block is fine\n");
				}
			}
			tmp_pos = statusvars->lastxblock * statusvars->xblocksize ;
			debug(DEBUG_EXCLUDE, "debug: checking with xblock %lli at %llu\n", statusvars->lastxblock , tmp_pos);
			tmp_bytes = 0; // using this as indicator for restart
			while (tmp_pos < statusvars->readposition + statusvars->startoffset + statusvars->remain ) {
				if (tmp_pos+ statusvars->xblocksize > statusvars->readposition + statusvars->startoffset ) {
					if (tmp_pos <= statusvars->readposition + statusvars->startoffset ) {
						// start of current block is covered by exclude block. skip.
						debug(DEBUG_EXCLUDE, "debug: skipping ahead to avoid exclude block\n");
						tmp_bytes = 1;
						if (tmp_pos+ statusvars->xblocksize < statusvars->readposition + statusvars->startoffset + statusvars->remain ) {
							statusvars->remain = (tmp_pos+ statusvars->xblocksize )-( statusvars->readposition + statusvars->startoffset );
							debug(DEBUG_EXCLUDE, "debug: remain set to %llu\n", statusvars->remain );
						} else {
							if (! statusvars->backtracemode ) {
								statusvars->remain = 0;
							} else {
								statusvars->remain = 1;
								debug(DEBUG_EXCLUDE, "debug: remain set to %llu\n", statusvars->remain );
							}
						}
						statusvars->readposition = (tmp_pos+ statusvars->xblocksize )- statusvars->startoffset ;
						break;
					} else if (tmp_pos < statusvars->readposition + statusvars->startoffset + statusvars->remain ) {
						debug(DEBUG_EXCLUDE, "debug: shrinking block size because of exclude block from %llu to %llu\n", statusvars->remain ,(tmp_pos-( statusvars->readposition + statusvars->startoffset )));
						statusvars->remain = tmp_pos-( statusvars->readposition + statusvars->startoffset );
						break;
					} else {
						// start of exclude block is beyond end of our area. we are done
						break;
					}
				} else {
					// read next exclude block
					debug(DEBUG_EXCLUDE, "debug: reading another exclude block\n");
					tmp = fgets( statusvars->textbuffer , 64, statusvars->xblocksin);
					long long unsigned int dummy;
					if (sscanf( statusvars->textbuffer , "%llu", &dummy) != 1) tmp = NULL;
					if ( tmp!=NULL && (signed)dummy < (signed)statusvars->lastxblock ) {
						fprintf(stderr, "Parse error in badblocks file %s: not sorted correctly!\n", configvars->xblocksinfile);
						wantabort = 2;
						break;
					}
					tmp_pos = dummy;
					if (tmp == NULL) {
						// no more bad blocks in input file
						break;
					}
					statusvars->previousxblock = statusvars->lastxblock ;
					statusvars->lastxblock = tmp_pos;
					tmp_pos = statusvars->lastxblock * statusvars->xblocksize ;
					debug(DEBUG_EXCLUDE, "debug: reading another exclude block: %lli at %llu\n", statusvars->lastxblock , tmp_pos);
				}
			}
			if (tmp_bytes == 1) {
				debug(DEBUG_EXCLUDE, "debug: recalculation needed because of exclude blocks, restarting cycle\n");
				continue;
			}
		}
		if (wantabort) break;

// 6.b navigation - attempt to seek to requested input file position and find out actual position

		// seek and read - timed
		gettimeofday(& statusvars->oldtime , NULL);
		// seek only if the current file position differs from requested file position
		if ( statusvars->sposition != statusvars->readposition + statusvars->startoffset ) {
			debug(DEBUG_SEEK, "debug: seeking in input from %llu to %llu\n", statusvars->sposition , statusvars->readposition + statusvars->startoffset );
			statusvars->cposition = lseek(statusvars->source, statusvars->readposition + statusvars->startoffset , SEEK_SET);
			if ( statusvars->cposition > 0) {
				statusvars->sposition = statusvars->cposition ;
				statusvars->seekable = 1;
			} else {
				// seek failed, check why
				errtmp = errno;
				if (( statusvars->readposition + statusvars->startoffset ) < statusvars->filesize && lowlevel_canseek() && ( statusvars->lowlevel == 2 || ( statusvars->lowlevel == 1 && statusvars->desperate ))) {
					// lowlevel operation will take care of seeking.
					// and can do it
					statusvars->cposition = statusvars->readposition + statusvars->startoffset ;
					statusvars->sposition = statusvars->cposition ;
				} else if (errtmp == EINVAL && statusvars->seekable == 1) {
					// tried to seek past the end of file.
					break;
				} else {
					// input file is not seekable!
					if ( statusvars->seekable ) {
						write(1, &"|/|", 3);
						statusvars->seekable = 0;
					}
					if ( statusvars->readposition + statusvars->startoffset > statusvars->sposition ) {
						// emergency seek will only handle positive seeks
						// close input file for seek/skip
						close (statusvars->source);
						statusvars->cposition = emergency_seek( statusvars->startoffset + statusvars->readposition , statusvars->sposition , statusvars->blocksize , configvars->seekscriptfile);
						if ( statusvars->cposition < 0 && statusvars->newerror == 0) {
							// bail if we cannot skip over hard errors!
							fprintf(stderr, "\nError: Unable to skip over bad area.\n");
							break;
						}
						// reopen input file
						statusvars->source = open(configvars->sourcefile, O_RDONLY | O_NONBLOCK | statusvars->syncmode );
						if (statusvars->source == -1) {
							perror("\nError reopening sourcefile after external seek ");
							wantabort = 2;
							break;
						}
						if ( statusvars->cposition >= 0) {
							statusvars->sposition = statusvars->cposition ;
						}
					}
				}
			}
		}
		// prevent negative write offsets
		if ( statusvars->sposition > statusvars->startoffset ) {
			statusvars->readposition = statusvars->sposition - statusvars->startoffset ;
		} else {
			statusvars->readposition = 0;
		}
		// make sure not to read beyond the specified end
		if ( statusvars->length >= 0) {
			if ( statusvars->readposition >= statusvars->length ) statusvars->readposition = statusvars->length ;
			if ( statusvars->readposition + statusvars->remain >= statusvars->length ) statusvars->remain = statusvars->length - statusvars->readposition ;
		}
		// write where we read
		statusvars->writeposition = statusvars->readposition ;
		if ( statusvars->filesize > statusvars->startoffset ) {
			statusvars->percent = (100*( statusvars->readposition ))/( statusvars->filesize - statusvars->startoffset );
		}

// 6.c patience - wait for availability of data

		select_for_reading(statusvars->source, configvars, statusvars);
		if (wantabort) break;

// 6.d input - attempt to read from sourcefile
		// read input data
		// to limit memory usage do not allow to read chunks bigger
		// than one block, even if faultskipsize is set
		statusvars->maxremain = statusvars->remain ;
		if ( statusvars->maxremain > statusvars->blocksize ) statusvars->maxremain = statusvars->blocksize ;
		if ( statusvars->lowlevel == 0 || ( statusvars->lowlevel == 1 && ! statusvars->desperate )) {
			debug(DEBUG_IO, "debug: normal read\n");
			statusvars->block = read(statusvars->source, statusvars->databuffer , statusvars->maxremain );
		} else {
			//desperate mode means we are allowed to use low lvl 
			//IO syscalls to work around read errors
			debug(DEBUG_IO, "debug: low level read\n");
			statusvars->block = read_desperately(configvars->sourcefile, &statusvars->source, statusvars->databuffer , statusvars->sposition , statusvars->maxremain , statusvars->seekable , statusvars->desperate , statusvars->syncmode );
		}
		// time reading for quality calculation
		gettimeofday(& statusvars->newtime , NULL);
		statusvars->elapsed = timediff( statusvars->oldtime , statusvars->newtime );
		if (statusvars->timingfile) {
			fprintf(statusvars->timingfile, "%010llu %lu %lli\n", (long long)(statusvars->sposition / statusvars->blocksize) , statusvars->elapsed , (long long)statusvars->block );
		}

// 6.e feedback - calculate and display user feedback information

		// smooth times, react sensitive to high times
		if (timecategory( statusvars->elapsed ) > timecategory( statusvars->oldelapsed )) {
			statusvars->oldelapsed = (((9* statusvars->oldelapsed )+ statusvars->elapsed )/10);
		} else if (timecategory( statusvars->elapsed ) < timecategory( statusvars->oldelapsed )) {
			statusvars->oldelapsed = (((99* statusvars->oldelapsed )+ statusvars->elapsed )/100);
		}
		// update percentage if changed
		if ( statusvars->filesize && ( statusvars->percent != statusvars->oldpercent || statusvars->output )) {
			if (configvars->human) {
				printpercentage( statusvars->percent );
				printtimecategory(timecategory( statusvars->oldelapsed ));
			}
			statusvars->oldpercent = statusvars->percent ;
		}
		// update quality if changed
		if (timecategory( statusvars->oldelapsed ) != statusvars->oldcategory ) {
			if (configvars->human) {
				if ( statusvars->filesize ) printpercentage( statusvars->percent );
				printtimecategory(timecategory( statusvars->oldelapsed ));
			}
			statusvars->oldcategory = timecategory( statusvars->oldelapsed );
		} else if ( statusvars->output && configvars->human) {
			// or if any output has taken place
			if ( statusvars->filesize ) printpercentage( statusvars->percent );
			printtimecategory(timecategory( statusvars->oldelapsed ));
		}
		// break lines after 40 chars to prevent backspace bug of terminals
		if ( statusvars->linewidth > 40) {
			if (configvars->human) {
				tmp_pos = statusvars->readposition / statusvars->blocksize ;
				sprintf( statusvars->textbuffer , " [%lli] \n", (long long)tmp_pos);
				write(2, statusvars->textbuffer , strlen( statusvars->textbuffer ));
			}
			statusvars->linewidth = 0;
		}
		statusvars->output = 0;

// 6.f processing - react according to result of read operation
// 6.f.1 successful read:
		if ( statusvars->block > 0) {
			debug(DEBUG_IO, "debug: successful read\n");
			statusvars->sposition = statusvars->sposition + statusvars->block ;
			// successful read, if happening during soft recovery
			// (downscale or retry) list a single soft error
			if ( statusvars->newsofterror == 1) {
				statusvars->newsofterror = 0;
				statusvars->softerr ++;
			}
			// read successful, test for end of damaged area
			if ( statusvars->newerror == 0) {
// 6.f.1.a attempt to backtrack for readable data prior to current position or...
				// we are in recovery since we just read past the end of a damaged area
				// so we go back until the beginning of the readable area is found (if possible)

				if ( statusvars->remain > statusvars->resolution && statusvars->seekable ) {
				 	statusvars->remain = statusvars->remain /2;
					statusvars->readposition -= statusvars->remain ;
					write(1, &"<", 1);
					statusvars->output = 1;
					statusvars->linewidth ++;
					statusvars->backtracemode = 1;
					debug(DEBUG_FLOW, "debug: shrinking remain to %llu\n", statusvars->remain );
				} else {
					statusvars->newerror = statusvars->retries ;
					statusvars->remain = 0;
					tmp_pos = statusvars->readposition / statusvars->blocksize ;
					tmp_bytes = statusvars->readposition - statusvars->lastgood ;
					statusvars->damagesize += tmp_bytes;
					sprintf( statusvars->textbuffer , "}[%llu](+%llu)", (long long)tmp_pos, (long long)tmp_bytes);
					write(1, statusvars->textbuffer , strlen( statusvars->textbuffer ));
					if (configvars->human) write(2, &"\n", 1);
					statusvars->output = 1;
					statusvars->linewidth = 0;
					statusvars->backtracemode = 0;
					markoutput("end of bad area", statusvars->readposition , statusvars->lasterror , configvars, statusvars);
					statusvars->lasterror = statusvars->readposition ;
				}
				
			} else {
// 6.f.1.b write to output data file
				//disable desperate mode (use normal high lvl IO)
				statusvars->desperate = 0;
				statusvars->newerror = statusvars->retries ;
				if ( statusvars->block < statusvars->remain ) {
					// not all data we wanted got read, note that
					write(1, &"_", 1);
					statusvars->output = 1;
					statusvars->linewidth ++;
					statusvars->counter = 1;
				} else if (-- statusvars->counter <= 0) {
					write(1, &".", 1);
					statusvars->output = 1;
					statusvars->linewidth ++;
					statusvars->counter = 1024;
				}

				statusvars->remain -= statusvars->block ;
				statusvars->readposition += statusvars->block ;
				statusvars->writeremain = statusvars->block ;
				off_t writeoffset = 0;
				if ( statusvars->sposition < statusvars->startoffset ) {
					// handle cases where unwanted data has been read doe to seek error
					if (( statusvars->sposition + statusvars->block ) <= statusvars->startoffset ) {
						// we read data we are supposed to skip! Ignore.
						statusvars->writeremain = 0;
					} else {
						// partial skip
						statusvars->writeremain = ( statusvars->sposition + statusvars->block )- statusvars->startoffset ;
						writeoffset = statusvars->startoffset - statusvars->sposition ;
					}
				}
				while ( statusvars->writeremain > 0) {
					// write data to destination file
					debug(DEBUG_SEEK, "debug: seek in destination file: %llu\n", statusvars->writeposition );
					statusvars->cposition = lseek(statusvars->destination, statusvars->writeposition , SEEK_SET);
					if ( statusvars->cposition < 0) {
						fprintf(stderr, "\nError: seek() in %s failed", configvars->destfile);
						perror(" ");
						wantabort = 2;
						break;
					}
					debug(DEBUG_IO, "debug: writing data to destination file: %llu bytes at %llu\n", statusvars->writeremain , statusvars->cposition );
					statusvars->writeblock = write(statusvars->destination, statusvars->databuffer +writeoffset, statusvars->writeremain );
					if ( statusvars->writeblock <= 0) {
						fprintf(stderr, "\nError: write to %s failed", configvars->destfile);
						perror(" ");
						wantabort = 2;
						break;
					}
					statusvars->writeremain -= statusvars->writeblock ;
					writeoffset += statusvars->writeblock ;
					statusvars->writeposition += statusvars->writeblock ;
				}
			}
		} else if ( statusvars->block < 0) {
// 6.f.2 failed read
			debug(DEBUG_IO, "debug: read failed: %s\n", strerror(errno));
			// operation failed
			statusvars->counter = 1;
			// use low level IO for error correction if allowed
			statusvars->desperate = 1;
// 6.f.2.a try again or...
			if ( statusvars->remain > statusvars->resolution && statusvars->newerror > 0) {
				// start of a new erroneous area - decrease readsize in
				// case we can read partial data from the beginning
				statusvars->newsofterror = 1;
				statusvars->remain = statusvars->remain /2;
				write(1, &">", 1);
				statusvars->linewidth ++;
				statusvars->output = 1;
			} else {
				if ( statusvars->newerror > 1) {
					// if we are at minimal size, attempt a couple of retries
					statusvars->newsofterror = 1;
					statusvars->newerror --;
					write(1, &"!", 1);
					statusvars->linewidth ++;
					statusvars->output = 1;
				} else {
// 6.f.2.b skip over bad area
					// readsize is already minimal, out of retry attempts
					// unrecoverable error, go one sector ahead and try again there 

					if ( statusvars->newerror == 1) {
						// if this is still the start of a damaged area,
						// also print the amount of successfully read sectors
						statusvars->newsofterror = 0;
						statusvars->newerror = 0;
						tmp_pos = statusvars->readposition / statusvars->blocksize ;
						tmp_bytes = statusvars->readposition - statusvars->lasterror ;
						sprintf( statusvars->textbuffer , "[%llu](+%llu){", (long long)tmp_pos, (long long)tmp_bytes);
						write(1, statusvars->textbuffer , strlen( statusvars->textbuffer ));
						statusvars->output = 1;
						statusvars->linewidth += strlen( statusvars->textbuffer );
						// and we set the read size high enough to go over the damaged area quickly
						// (next block boundary)
						statusvars->remain = ((( statusvars->readposition / statusvars->blocksize )* statusvars->blocksize )+ statusvars->faultblocksize )- statusvars->readposition ;
						statusvars->lastgood = statusvars->readposition ;
					} 

					if (! statusvars->backtracemode ) {
						statusvars->harderr ++;
						write(1, &"X", 1);
						statusvars->output = 1;
						statusvars->linewidth ++;
						markoutput("continuous bad area", statusvars->readposition ,( statusvars->lasterror > statusvars->lastgood ? statusvars->lasterror : statusvars->lastgood ), configvars, statusvars);
						statusvars->lasterror = statusvars->readposition ;
					}

					// skip ahead(virtually)
					statusvars->readposition += statusvars->remain ;
					if (! statusvars->backtracemode ) {
						// force re-calculation of next blocksize to fix
						// block misalignment caused by partial data reading.
						// doing so during backtrace would cause an infinite loop.
						statusvars->remain = 0;
					} else {
						debug(DEBUG_FLOW, "debug: bad block during backtrace - remain is %llu at %llu\n", statusvars->remain , statusvars->readposition + statusvars->startoffset );
					}

				}

			}

			if (wantabort) break;

// 6.f.2.c close and reopen source file
			// reopen source file to clear possible error flags preventing us from getting more data
			close(statusvars->source);
			// do some forced seeks to move head around.
			for ( statusvars->cseeks = 0; statusvars->cseeks < statusvars->seeks ; statusvars->cseeks ++) {
				debug(DEBUG_SEEK, "debug: forced head realignment\n");
				// note. must use O_RSYNC since with O_DIRECT / raw devices, lseek to end of file might not work
				statusvars->source = open(configvars->sourcefile, O_RDONLY | O_NONBLOCK | O_RSYNC );
				if (statusvars->source) {
					lseek(statusvars->source, 0, SEEK_SET);
					select_for_reading(statusvars->source, configvars, statusvars);
					if (wantabort) break;
					read(statusvars->source, statusvars->databuffer , statusvars->blocksize );
					close(statusvars->source);
				}
				statusvars->source = open(configvars->sourcefile, O_RDONLY | O_NONBLOCK | O_RSYNC );
				if (statusvars->source) {
					lseek(statusvars->source,- statusvars->blocksize , SEEK_END);
					select_for_reading(statusvars->source, configvars, statusvars);
					if (wantabort) break;
					read(statusvars->source, statusvars->databuffer , statusvars->blocksize );
					close(statusvars->source);
				}
				if (wantabort) break;
			}
			statusvars->source = open(configvars->sourcefile, O_RDONLY | O_NONBLOCK | statusvars->syncmode );
			while (statusvars->source == -1 && statusvars->forceopen && !wantabort ) {
			 	sleep(1);
				statusvars->source = open(configvars->sourcefile, O_RDONLY | O_NONBLOCK | statusvars->syncmode );
			}
			if (statusvars->source == -1) {
				perror("\nError reopening sourcefile after read error ");
				wantabort = 2;
				break;
			}
			if ( statusvars->seekable ) {
				// in seekable input, a re-opening sets the pointer to zero
				// we must reflect that.
				statusvars->sposition = 0;
			}
		}
	}

// 7.closing and finalisation

	debug(DEBUG_FLOW, "debug: main loop ended\n");
	fflush(stdout);
	fflush(stderr);
	if ( statusvars->newerror == 0) {
		// if theres an error at the end of input, treat as if we had one successful read afterwards
		tmp_pos = statusvars->readposition / statusvars->blocksize ;
		tmp_bytes = statusvars->readposition - statusvars->lastgood ;
		statusvars->damagesize += tmp_bytes;
		sprintf( statusvars->textbuffer , "}[%llu](+%llu)", (long long)tmp_pos, (long long)tmp_bytes);
		write(1, statusvars->textbuffer , strlen( statusvars->textbuffer ));
		if (configvars->human) write(2, &"\n", 1);
		// mark badblocks in output if not aborted because of error
		if (wantabort<2) {
			if ( statusvars->filesize && statusvars->lastgood + statusvars->startoffset < statusvars->filesize && statusvars->readposition + statusvars->startoffset >= statusvars->filesize ) {
				markoutput("end of file - filling to filesize", statusvars->filesize, statusvars->lastgood, configvars, statusvars);
			} else {
				markoutput("end of file - filling to last seen position", statusvars->readposition + statusvars->startoffset, statusvars->lastgood, configvars, statusvars);
			}
		}
	}
	if (wantabort==1) {
		fprintf(stdout, "\nAborted by user request!\n");
	} else if (wantabort>1) {
		fprintf(stdout, "\nAborted because of error!\n");
	} else {
		fprintf(stdout, "\nDone!\n");
	}
	fprintf(stdout, "Recovered bad blocks: %llu\n", (long long)statusvars->softerr );
	fprintf(stdout, "Unrecoverable bad blocks (bytes): %llu (%llu)\n", (long long)statusvars->harderr, (long long)statusvars->damagesize );
	fprintf(stdout, "Blocks (bytes) copied: %llu (%llu)\n", (long long)(( statusvars->writeposition )/ statusvars->blocksize ), (long long)statusvars->writeposition );

	close(statusvars->destination);
	close(statusvars->source);
	if ( statusvars->incremental == 1) fclose(statusvars->bblocksin);
	if ( statusvars->excluding == 1) fclose(statusvars->xblocksin);
	if (configvars->bblocksoutfile != NULL) close(statusvars->bblocksout);
	if (statusvars->timingfile != NULL) fclose(statusvars->timingfile);
	arglist_kill(carglist);
	if (wantabort) {
		return (2);
	}
	if ( statusvars->damagesize > 0) {
		return (1);
	}
	return (0);
}
