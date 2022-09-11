/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
#define _FILE_OFFSET_BITS 64
// make off_t a 64 bit pointer on system that support it

#ifndef __linux__
#include <sys/types.h>
// if we don't have linux, the used ioctrls will be different
// use a dummy read function that uses high lvl operations
off_t read_desperately(char* filename, int *fd, char* buffer,
			off_t position, off_t length,
			int seekable, int recovery, int syncmode) {
	off_t retval;
	retval=read(*fd,buffer,length);
	return retval;
}
off_t lowlevel_filesize(char* filename, off_t filesize) {
	return filesize;
}
off_t lowlevel_blocksize(char* filename, off_t blocksize) {
	return blocksize;
}
int lowlevel_canseek() {
	return 0;
}
#else

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <errno.h>
#include <stdio.h>


//--------------floppy-------------------------------
int is_floppy(int fd) {
	static int isfloppy=-1;
	if (isfloppy!=-1) return isfloppy;

	// attempt a drive reset, return true if successful
	if (ioctl(fd,FDRESET,FD_RESET_ALWAYS)>=0) {
		fprintf(stderr,"\nFloppy low level access: drive reset, twaddle ioctl\n");
		isfloppy=1;
	// or at least supported (a "real" reset fails if not root)
	} else if (errno==EACCES || errno==EPERM) {
		fprintf(stderr,"\nFloppy low level access: twaddle ioctl\n");
		isfloppy=1;
	} else {
		isfloppy=0;
	}
	return isfloppy;
}

void reset_floppy(int fd) {
	// attempt a drive reset
	ioctl(fd,FDRESET,FD_RESET_ALWAYS);
}

void torture_floppy(int fd) {
	// send the twaddle system call
	ioctl(fd,FDTWADDLE);
}
//--------------end of floppy stuff -----------------


//---------------CD/DVD READING ---------------------

int cdromsectorsize=0;
int cdromsectoroffset=16;

// is_cd queries disc-status
int is_cd(int fd) {
	static int iscd=-1;
	int retval;
	char* mode;
	if(iscd!=-1) return iscd;
	retval=ioctl(fd,CDROM_DISC_STATUS);
	if (retval>=0) {
		iscd=1;
		if (retval==CDS_AUDIO) {
			cdromsectorsize=2352;
			cdromsectoroffset=0;
			mode="audio";
		} else if (retval==CDS_DATA_1) {
			mode="Mode1";
			cdromsectorsize=2048;
			cdromsectoroffset=16;
		} else if (retval==CDS_DATA_2) {
			mode="Mode2";
			cdromsectorsize=2336;
			cdromsectoroffset=16;
		} else if (retval==CDS_XA_2_1) {
			mode="XA 1";
			cdromsectorsize=2048;
			cdromsectoroffset=24;
		} else if (retval==CDS_XA_2_2) {
			mode="XA 2";
			cdromsectorsize=2324;
			cdromsectoroffset=24;
		} else if (retval==CDS_MIXED) {
			fprintf(stderr,"CDROM mixed mode - low level access: drive reset\n");
			cdromsectorsize=0;
			cdromsectoroffset=0;
			return 1;
		} else {
			fprintf(stderr,"CDROM unknown disc - low level access: drive reset\n");
			cdromsectorsize=0;
			cdromsectoroffset=0;
			return 1;
		}
		fprintf(stderr,"CDROM %s - low level access: drive reset, raw read\n",mode);
	} else {
		iscd=0;
	}
	return iscd;
}
// get blocksize and filesize info
off_t blocksize_cd(int fd, off_t blocksize) {
	if (cdromsectorsize!=0) {
		fprintf(stderr,"CDROM low level block size: %u\n",cdromsectorsize);
		blocksize=cdromsectorsize;
	}
	return blocksize;
}
off_t filesize_cd(int fd, off_t filesize) {
	long result;
	int retval=ioctl(fd,CDROM_LAST_WRITTEN,&result);
	if (retval==0 && cdromsectorsize>0) {
		fprintf(stderr,"CDROM low level disk size: %lu\n",cdromsectorsize*(result));
		filesize=cdromsectorsize*(result);
	}
	return filesize;
}

// is_dvd queries dvd layer information
int is_dvd(int fd) {
	static int isdvd=-1;
	if(isdvd!=-1) return isdvd;
	dvd_struct s;
	s.type=DVD_STRUCT_PHYSICAL;
	if (ioctl(fd,DVD_READ_STRUCT,&s)>=0) {
		isdvd=1;
		fprintf(stderr,"DVD low level access: drive reset\n");
	} else {
		isdvd=0;
	}
	return isdvd;
}

// issue a drive/bus reset
void reset_cd(int fd) {
	ioctl(fd,CDROMRESET);
}

// helper function to calculate a cd sector position
void lba_to_msf( off_t lba, struct cdrom_msf * msf) {
	//lba= (((msf->cdmsf_min0*CD_SECS) + msf->cdmsf_sec0) * CD_FRAMES + msf->cdmsf_frame0 ) - CD_MSF_OFFSET;
	lba=lba+CD_MSF_OFFSET;
	//lba'= ((msf->cdmsf_min0*CD_SECS) + msf->cdmsf_sec0) * CD_FRAMES + msf->cdmsf_frame0;
	msf->cdmsf_frame0=lba % CD_FRAMES;
	lba=lba/CD_FRAMES;
	//lba''= (msf->cdmsf_min0*CD_SECS) + msf->cdmsf_sec0;
	msf->cdmsf_sec0=lba % CD_SECS;
	msf->cdmsf_min0=lba/CD_SECS;
	//lba'''= msf->cdmsf_min0;
}
// read raw mode sector from a cd
off_t read_from_cd(int fd, unsigned char* buffer, off_t position, off_t length) {

	unsigned char blockbuffer[CD_FRAMESIZE_RAWER];
	struct cdrom_msf *msf=(struct cdrom_msf*)blockbuffer;

	// the calculation silently assumes that the cd
	// has a consistent sector size and offset all over.
	// this is not true for mixed mode cds.
	// TODO: read TOC for cd layout
	// and fix sector calculation accordingly
	if (cdromsectorsize==0) {
		return(read(fd,buffer,length));
	}
	off_t lba=position/cdromsectorsize;
	off_t extra=position-(lba*cdromsectorsize);
	off_t xlength=cdromsectorsize-extra;

	if (xlength>length) xlength=length;
	lba_to_msf(lba,msf);
	if (ioctl(fd, CDROMREADRAW, msf) == -1) {
		return -1;
	}

	// TODO: read parity and LEC data and check for possible read errors
	
	//each physical cd sector has 12 bytes "sector lead in thingy"
	//and 4 bytes address (maybe one could really confuse cdrom drives
	//by putting a very similar structure within user data?)
	memcpy(buffer,(blockbuffer+extra+cdromsectoroffset),xlength);
	lseek(fd,position+xlength,SEEK_SET);
	return xlength;

}
//--------------end of CD/DVD stuff -----------------

// tries to perform a low level read operation on a device
// it should already be seeked to the right position for normal read()
// possible results:
// return>0: amounts of bytes read, the device will be open and the internal
// seek pointer point to position+length
// return<0: error. the device will be open but in undefined condition
off_t read_desperately(char* filename, int *fd, unsigned char* buffer,
			off_t position, off_t length,
			int seekable, int recovery, int syncmode) {
	off_t retval;
	
	if (is_dvd(*fd)) {
		//linux dvdrom driver doesn't provide reasonably documented
		//low level reading syscalls, but we can do a device reset.
		if (recovery) {
			reset_cd(*fd);
			//reopen device after reset
			close(*fd);
			*fd=open(filename,O_RDONLY | O_NONBLOCK | syncmode );
			if (*fd<=0) return -1;
			lseek(*fd,position,SEEK_SET);
		}
		retval=read(*fd,buffer,length);
		return retval;
	} else if (is_cd(*fd)) {
		//cdroms have full scale low level ioctls available for us
		//however safecopy doesnt support the full redcode standard
		//and some cheap assumptions are made
		//regarding the cdrom disk layout
		//(mode1 single track single session cds only)
		if (recovery) {
			reset_cd(*fd);
			//reopen device after reset
			close(*fd);
			*fd=open(filename,O_RDONLY | O_NONBLOCK | syncmode );
			if (*fd<=0) return -1;
		}
		retval=read_from_cd(*fd,buffer,position,length);
		return retval;
	} else if (is_floppy(*fd)) {
		//the linux floppy driver does cracy low level stuff, but
		//for sanity reasons we support reset and waddle ioctl only
		if (recovery) {
			reset_floppy(*fd);
			//reopen device after reset
			close(*fd);
			*fd=open(filename,O_RDONLY | O_NONBLOCK | syncmode );
			if (*fd<=0) return -1;
			lseek(*fd,position,SEEK_SET);
		}
		retval=read(*fd,buffer,length);
		if (retval<0 && errno==EIO) {
			//try again with "twaddle" if failed
			reset_floppy(*fd);
			//reopen device after reset
			close(*fd);
			*fd=open(filename,O_RDONLY | O_NONBLOCK | syncmode );
			if (*fd<=0) return -1;
			lseek(*fd,position,SEEK_SET);
			//play around with drive engine
			torture_floppy(*fd);
			retval=read(*fd,buffer,length);
		}
		return retval;
	} else {
		// unsupported or unknown device, attempt a normal read
		retval=read(*fd,buffer,length);
		return retval;
	}
}
// tries to get some generic driver information
off_t lowlevel_filesize(char* filename, off_t filesize) {
	int inf=open(filename, O_RDONLY);
	off_t result;
	if (is_dvd(inf)) {
		result=filesize;
	} else if (is_cd(inf)) {
		result=filesize_cd(inf,filesize);
	} else {
		result=filesize;
	}
	close(inf);
	return result;
}
off_t lowlevel_blocksize(char* filename, off_t blocksize) {
	int inf=open(filename, O_RDONLY);
	off_t result;
	if (is_dvd(inf)) {
		result=blocksize;
	} else if (is_cd(inf)) {
		result=blocksize_cd(inf,blocksize);
	} else {
		result=blocksize;
	}
	close(inf);
	return result;
}
int lowlevel_canseek() {
	if (is_cd(0)) return 1;
	return 0;
}
#endif //__linux__
