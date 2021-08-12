
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint32;
typedef signed int int32;
//typedef int32 ssize_t;
//typedef uint32 size_t;

#define nil NULL




//////////// NB: for non-devkits REBOOT_IOP and the CDROM ones have to be defined!
// this disconnects dsedb if run with -r run foo.elf
// but it works if you run from within dsedb
#define REBOOT_IOP
#define IOP_CDROM	// load IOP modules from CD
// these two should be defined automatically as needed
//#define CDROM		// enable CD support
//#define HOSTFS		// enable host support

// get IOP modules from host instead of cdrom
//#define IOP_HOST

// get normal files from host instead of cdrom
//#define FILES_HOST

// stream from host instead of cdrom
//#define STREAM_HOST	// not real streaming, but can read from gta3.img

// load files from gta3.dir/img, else from ./models/
// needs CDROM right now
#define USE_CDIMAGE

#if defined(IOP_HOST) || defined(FILES_HOST) || defined(STREAM_HOST)
#define HOSTFS
#endif
#if !defined(IOP_HOST) || !defined(FILES_HOST) || !defined(STREAM_HOST)
#define CDROM
#endif
