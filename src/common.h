
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint32;
typedef signed int int32;
//typedef int32 ssize_t;
//typedef uint32 size_t;

#define nil NULL




//////////// NB: for non-devkits these three have to be defined!!!
// this disconnects dsedb if run with -r run foo.elf
// but it works if you run from within dsedb
//#define REBOOT_IOP
//#define IOP_CDROM	// load IOP modules from CD
#define CDROM		// enable CD support

// load files from gta3.dir/img, else from ./models/
// needs CDROM right now
#define USE_CDIMAGE
