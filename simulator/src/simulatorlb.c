/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
#define _FILE_OFFSET_BITS 64
#include <config.h>

#ifdef USE_GNU_SOURCE
#define _GNU_SOURCE
#endif

#define CONFIGFILE "simulator.cfg"
#define MAXLIST 1000000

#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
//#include <unistd.h>
#include <string.h>
//#include <fcntl.h>
#ifndef _FCNTL_H
#define _FCNTL_H        1
#include <bits/fcntl.h>
#endif
#include <stdarg.h>

static int (*realopen)(const char*,int, ...);
static int (*realclose)(int);
static off_t (*reallseek)(int,off_t,int);
static ssize_t (*realread)(int,void*,size_t);

void _init(void);
int open(const char*,int,...);
int open64(const char*,int,...);
off_t lseek(int,off_t,int);
off64_t lseek64(int,off64_t,int);
ssize_t read(int,void*,size_t);
int close(int);

// these are needed on some newer glibc:
int __open_2(const char*,int);
int __open64_2(const char*,int);

ssize_t write(int, const void *, size_t);

struct timedata {
	off64_t sector;
	int failtime;
	int goodtime;
};

static int mydesc=-1;
static off_t current=0;
static off_t oldcurrent=0;

static struct timeval starttime,endtime;
static unsigned long granularity, sleeptime, seeksleeptime;

static struct timedata slowsector[MAXLIST];
static struct timedata slowsector[MAXLIST];
static struct timedata softerror[MAXLIST];
static int softerrorcount[MAXLIST];
static struct timedata harderror[MAXLIST];
static unsigned long slowsectordelay=0;
static unsigned long sectorsleeptime=0;
static int slowsectors=0;
static int slowsectorptr=0;
static int softerrors=0;
static int softerrorptr=0;
static int harderrors=0;
static int harderrorptr=0;
static int blocksize=1024;
static int softfailcount=0;
static int filesize=10240;
static char filename[256]="/dev/urandom";
static int verbosity=1;

// calculate difference in usecs between two struct timevals
static inline long int timediff() {
	static long int usecs;
	usecs=endtime.tv_usec-starttime.tv_usec;
	usecs=usecs+((endtime.tv_sec-starttime.tv_sec)*1000000);
	return usecs;

}

static inline void dodelay(int factor) {
	static unsigned long x;
	x=1;
	x=x<<factor;
	sleeptime=(x*slowsectordelay);
}

static inline void adddelay(int factor) {
	static unsigned long x;
	x=1;
	x=x<<factor;
	sleeptime+=(x*slowsectordelay);
}

static inline void dosleep() {
	// sleep for the remaining time (if any)
	// if remaining time is smaller than  granularity
	// we are evil and do busy waiting
	static struct timespec x;
	static unsigned long timed;
	sleeptime=sleeptime+seeksleeptime;
	seeksleeptime=0;

	gettimeofday(&endtime,NULL);
	timed=timediff();
	while (timed<sleeptime) {
		if (sleeptime-timed>granularity) {
			x.tv_sec=0;
			x.tv_nsec=1;
			nanosleep(&x,NULL);
		}
		gettimeofday(&endtime,NULL);
		timed=timediff();
	}

}

static inline void sectorsleep() {
	// add to sleep time the time needed to seek to the current position
	static long difference;
	difference=(oldcurrent-current)/blocksize;
	oldcurrent=current;
	if (difference<0) difference=-difference;
	seeksleeptime=((difference*sectorsleeptime)/1000);
}

static void addtolist(struct timedata *array,int *count,struct timedata *value) {
// insert value into sorted array of length *count
	if (*count==MAXLIST) {
		fprintf(stderr,"simulator error: cannot store any more sectors in list - out of hardcoded memory limit!\n");
		return;
	}
	if (!*count) {
		// first element in list
		array[*count].sector=value->sector;
		array[*count].failtime=value->failtime;
		array[*count].goodtime=value->goodtime;
		*count=1;
	} else {
		if (value->sector>array[*count-1].sector) {
			// default case, add to end of list
			array[*count].sector=value->sector;
			array[*count].failtime=value->failtime;
			array[*count].goodtime=value->goodtime;
			*count=*count+1;
		} else {
			// stupid case, add somewhere else
			int t;
			for (t=*count-1; t>=0;t--) {
				// go back in the list to see where it has to be stuck in
				if (array[t].sector==value->sector) {
					// unless its already there
					return;
				} else if (array[t].sector<value->sector) {
					// no it wasnt, here we stick it in.
					memmove(&array[t+2],&array[t+1],((MAXLIST-t)-2)*sizeof(struct timedata));
					array[t+1].sector=value->sector;
					array[t+1].failtime=value->failtime;
					array[t+1].goodtime=value->goodtime;
					*count=*count+1;
					return;
				}
			}
			// hmm we went through the entire list and only bigger elements
			// guess we have to stick it at the very start
			memmove(&array[1],&array[0],(MAXLIST-1)*sizeof(struct timedata));
			array[0].sector=value->sector;
			array[0].failtime=value->failtime;
			array[0].goodtime=value->goodtime;
			*count=*count+1;
			return;
		}
	}
}

static inline int isinlist(struct timedata *array, int *pos, int *count,off64_t sector) {
// tell if a value is ina sorted list, speed up assuming the list is asked sequentially
	if (!*count) return 0;
	if (sector<array[0].sector) return 0;
	if (sector==array[0].sector) {
		*pos=0;
		return 1;
	}
	if (sector>array[*count-1].sector) return 0;
	if (sector==array[*count-1].sector) {
		*pos=*count-1;
		return 1;
	}
	// move current pointer forward if necessary
	while (*pos<*count-1 && sector>array[*pos].sector) {
		*pos=*pos+1;
	}
	// move current pointer backward if necessary
	while (*pos>0 && sector<array[*pos].sector) {
		*pos=*pos-1;
	}
	// check wether we are where we want to be
	if (array[*pos].sector==sector) return 1;
	return 0;
}


void readoptions() {
	FILE *fd;
	char line[256];
	char *number;
	int t;
	struct timedata x;
	struct timespec tx;
	long long unsigned int tmp;

	softerrors=0;
	harderrors=0;
	fd=fopen(CONFIGFILE,"r");
	if (!fd) {
		perror("simulator could not open config file "CONFIGFILE);
		return;
	}
	gettimeofday(&starttime,NULL);
	for (t=0;t<10;t++) {
		tx.tv_sec=0;
		tx.tv_nsec=1;
		nanosleep(&tx,NULL);
	}
	gettimeofday(&endtime,NULL);
	// granularity is set higher than it is by about 1/4
	granularity=timediff()/7;
	fprintf(stderr,"simulator time granularity: %lu usecs\nsimulator everything shorter will be busy-waiting\n",granularity);

	while (fgets(line,255,fd)) {
		number=strchr(line,'=');
		if (number) {
			*number=0;
			number++;
			current=0;
			x.sector=0;
			x.goodtime=0;
			x.failtime=0;
			tmp=0;
			if (strcmp(line,"verbose")==0) {
				sscanf(number,"%u",&verbosity);
				fprintf(stderr,"simulator verbosity: %u\n",verbosity);
			} else if (strcmp(line,"blocksize")==0) {
				sscanf(number,"%u",&blocksize);
				if (verbosity) fprintf(stderr,"simulator simulated blocksize: %u\n",blocksize);
			} else if (strcmp(line,"filesize")==0) {
				sscanf(number,"%u",&filesize);
				if (verbosity) fprintf(stderr,"simulator simulated filesize: %u\n",filesize);
			} else if (strcmp(line,"source")==0) {
				sscanf(number,"%s",filename);
				if (verbosity) fprintf(stderr,"simulator opening data source: %s\n",filename);
			} else if (strcmp(line,"softfailcount")==0) {
				sscanf(number,"%u",&softfailcount);
				if (verbosity) fprintf(stderr,"simulator simulated soft error count: %u\n",softfailcount);
			} else if (strcmp(line,"seekdelay")==0) {
				sscanf(number,"%lu",&sectorsleeptime);
				if (verbosity) fprintf(stderr,"simulator time to seek over 1000 sectors: %lu usec\n",sectorsleeptime);
			} else if (strcmp(line,"delay")==0) {
				sscanf(number,"%lu",&slowsectordelay);
				if (verbosity) fprintf(stderr,"simulator delay on any sectors: %lu usec\n",slowsectordelay);
			} else if (strcmp(line,"slow")==0) {
				sscanf(number,"%llu %u",&tmp,&x.goodtime);
				x.sector=tmp;
				addtolist(slowsector,&slowsectors,&x);
				if (verbosity) fprintf(stderr,"simulator simulating read difficulty in block: %llu\n",tmp);
			} else if (strcmp(line,"softfail")==0) {
				sscanf(number,"%llu %u %u",&tmp,&x.goodtime,&x.failtime);
				x.sector=tmp;
				addtolist(softerror,&softerrors,&x);
				if (verbosity) fprintf(stderr,"simulator simulating soft error in block: %llu\n",tmp);
				softerrorcount[softerrors]=0;
			} else if (strcmp(line,"hardfail")==0) {
				sscanf(number,"%llu %u",&tmp,&x.failtime);
				x.sector=tmp;
				addtolist(harderror,&harderrors,&x);
				if (verbosity) fprintf(stderr,"simulator simulating hard error in block: %llu\n",tmp);
			}
		}
	}
}


static inline void myprint(char* text) {
	static size_t len;
	len=0;
	if (!verbosity) return;
	while ((char) *(text+len)) len++;
	write(2,text,len);
}

static inline void myprinthex(unsigned int num) {
	char buffer[17];
	buffer[16]=0x0;
	unsigned int num2;
	num2=num;
	char current='0';
	int pos;
	pos=16;
	if (!verbosity) return;
	myprint("0x");
	if (num2==0) {
		myprint("0");
	} else {
		while (num2>0) {
			current=(num2 & 15);
			num2=num2/16;
			if (current<10) {
				current+=48;
			} else {
				current+=87;
			}
			buffer[--pos]=current;
		}
		myprint(&buffer[pos]);
	}
}
static inline void myprintint(unsigned int num) {
	char buffer[33];
	buffer[32]=0x0;
	unsigned int num2=num;
	char current='0';
	int pos=32;
	if (num2==0) {
		myprint("0");
	} else {
		while (num2>0) {
			current=(num2 % 10);
			num2=num2/10;
			current+=48;
			buffer[--pos]=current;
		}
		myprint(&buffer[pos]);
	}
}

void _init(void) {
	realopen=dlsym(RTLD_NEXT,"open");
	realclose=dlsym(RTLD_NEXT,"close");
	reallseek=dlsym(RTLD_NEXT,"lseek");
	realread=dlsym(RTLD_NEXT,"read");
	myprint("simulator initialising - reading config "CONFIGFILE"\n");
	readoptions();
}

int open(const char *pathname, int flags, ...) {
	int mode=0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start (ap,flags);
		mode=va_arg(ap,int);
		va_end(ap);
	}
	return open64(pathname,flags,mode);
}

int __open_2(const char *pathname, int flags) {
	return open64(pathname,flags);
}

int open64(const char *pathname,int flags,...) {
	//va_list ap;
	int fd;
	int mode=0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start (ap,flags);
		mode=va_arg(ap,int);
		va_end(ap);
	}
	if (strcmp(pathname,"debug")==0) {
		if (mydesc!=-1) {
			myprint("simulator open - file already openend, can't open twice!\n");
			errno=ETXTBSY;
			return -1;
		}
		mydesc=realopen(filename,O_RDONLY);
		if (mydesc==-1) {
			perror("simulator couldnt open source");
		} else {
			myprint("opening debug\n");
		}
		return mydesc;
	}
	fd=realopen(pathname,flags,mode);
	return fd;
}

int __open64_2(const char *pathname, int flags) {
	return open64(pathname,flags);
}

int close(int fd) {
	if (fd==mydesc && mydesc!=-1) {
		myprint("closing debug\n");
		mydesc=-1;
	}
	return realclose(fd);
}

off_t lseek(int filedes, off_t offset, int whence) {
	return lseek64(filedes,offset,whence);
}

off64_t lseek64(int filedes, off64_t offset, int whence) {
	off64_t newcurrent=0;
	if ( filedes==mydesc && mydesc!=-1) {
		if (whence==SEEK_SET) {
			myprint("seeking in debug: SEEK_SET to ");
			newcurrent=offset;
		} else if (whence==SEEK_CUR) {
			myprint("seeking in debug: SEEK_CUR to ");
			newcurrent=current+offset;
		} else if (whence==SEEK_END) {
			myprint("seeking in debug: SEEK_END to ");
			newcurrent=filesize+offset;
		} else {
			errno=EINVAL;
			return -1;
		}
		if (newcurrent>filesize) {
			errno=EINVAL;
			return -1;
		}
		current=newcurrent;
		myprintint(current);
		myprint("\n");
		reallseek(filedes,current,SEEK_SET);
		return current;
	}
	return reallseek(filedes,offset,whence);
}

ssize_t read(int fd,void *buf,size_t count) {
	ssize_t result;
	int count1;
	int block1,block2;
	int max=filesize;
	//myprint("read called\n");
	if ( fd==mydesc && mydesc!=-1) {
		gettimeofday(&starttime,NULL);
		result=count;
		myprint("reading from debug file: ");
		myprintint(count);
		myprint(" at position ");
		myprintint(current);

		if (current+count>filesize) {
			result=filesize-current;
			if (result<1) {
				myprint(" reads zero!\n");
				return 0;
			}
		}
		sectorsleep();
		block1=current/blocksize;
		block2=(current+result)/blocksize;
		if (isinlist(harderror,&harderrorptr,&harderrors,block1)) {
			myprint(" simulated hard failure!\n");
			dodelay(harderror[harderrorptr].failtime);
			dosleep();
			errno=EIO;
			return -1;
		} else if (isinlist(softerror,&softerrorptr,&softerrors,block1)) {
				if (softerrorcount[softerrorptr]++<softfailcount) {
					myprint(" simulated soft failure!\n");
					dodelay(softerror[softerrorptr].failtime);
					dosleep();
					errno=EIO;
					return -1;
				} else {
					if (softerrorcount[softerrorptr]>softfailcount+1) {
						// this code currently gets never reached, its just an example
						// of how inflicting permanent damage could be simulated
						myprint(" simulated soft failure turned hard!\n");
						dodelay(softerror[softerrorptr].failtime);
						dosleep();
						errno=EIO;
						return -1;
					}
					dodelay(softerror[softerrorptr].goodtime);
					myprint(" simulated soft recovery:");
					softerrorcount[softerrorptr]-=2;
				}
		} else if (isinlist(slowsector,&slowsectorptr,&slowsectors,block1)) {
			dodelay(slowsector[slowsectorptr].goodtime);
		} else {
			dodelay(0);
		}
		for (count1=block1+1;count1<=block2;count1++) {
			if (max>count1*blocksize) {
				if (isinlist(softerror,&softerrorptr,&softerrors,count1)) {
					max=count1*blocksize;
					break;
				} else if (isinlist(harderror,&harderrorptr,&harderrors,count1)) {
					max=count1*blocksize;
					break;
				} else if(count1*blocksize<current+result) {
					//ignore sector boundary address itself but add "touched" sectors to delay
					if (isinlist(slowsector,&slowsectorptr,&slowsectors,count1)) {
						adddelay(slowsector[slowsectorptr].goodtime);
					} else {
						adddelay(0);
					}
				}
			}
		}
		if (current+result>max) {
			myprint(" shrinks due to upcoming failure and ");
			result=max-current;
		}

		result= realread(fd,buf,result);
		current+=result;
		// no sectorsleep in continuous read - thats already calculated in through statistics gathering
		oldcurrent=current;
		myprint(" reads ");
		myprintint(result);
		myprint(" bytes\n");
		dosleep();
		return result;
	}
	return realread(fd,buf,count);
}
