#include <assert.h>
#include "rwcore.h"
#include "common.h"
#include "libcdvd.h"
#include "FileMgr.h"

char CFileMgr::ms_rootDirName[128] = "\\";
char CFileMgr::ms_dirName[128];

void SkySetDirectory(char *dir) {}	// TODO: skyfs.c

void
CFileMgr::Initialise(void)
{
	SkySetDirectory(ms_rootDirName);
}

void
CFileMgr::ChangeDir(const char *dir)
{
	if(*dir == '\\'){
		strcpy(ms_dirName, ms_rootDirName);
		dir++;
	}
	if(*dir != '\0'){
		strcat(ms_dirName, dir);
		if(dir[strlen(dir)-1] != '\\')
			strcat(ms_dirName, "\\");
	}
	SkySetDirectory(ms_dirName);
}

void
CFileMgr::SetDir(const char *dir)
{
	strcpy(ms_dirName, ms_rootDirName);
	if(*dir != '\0'){
		strcat(ms_dirName, dir);
		if(dir[strlen(dir)-1] != '\\')
			strcat(ms_dirName, "\\");
	}
	SkySetDirectory(ms_dirName);
}

// actually no mode on PS2
ssize_t
CFileMgr::LoadFile(const char *file, uint8 *buf, int maxlen, const char *mode)
{
	void *fp;
	int len, size;
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	fp = fileFuncs->rwfopen(file, "r");
	if(fp == nil)
		return -1;
	size = 0;
	// actually fwread returns size_t, so always positive...
	while(len = fileFuncs->rwfread(buf+size, 1, 0x4000, fp), len >= 0){
		size += len;
		if(size > maxlen)
			assert(0 && "read past buffer");
		if(len != 0x4000){
			fileFuncs->rwfclose(fp);
			return size;
		}
	}
	return -1;
}

int
CFileMgr::OpenFile(const char *file)
{
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	return (int)fileFuncs->rwfopen(file, "r");
}

int
CFileMgr::OpenFileForWriting(const char *file)
{
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	return (int)fileFuncs->rwfopen(file, "wo");
}

size_t
CFileMgr::Read(int fd, char *buf, ssize_t len)
{
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	return (int)fileFuncs->rwfread(buf, 1, len, (void*)fd);
}

size_t
CFileMgr::Write(int fd, const char *buf, ssize_t len)
{
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	return (int)fileFuncs->rwfwrite(buf, 1, len, (void*)fd);
}

bool
CFileMgr::Seek(int fd, int offset, int whence)
{
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	return (int)fileFuncs->rwfseek((void*)fd, offset, whence);
}

bool
CFileMgr::ReadLine(int fd, char *buf, int len)
{
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	return fileFuncs->rwfgets(buf, len, (void*)fd) > 0;
}

int
CFileMgr::CloseFile(int fd)
{
	RwFileFunctions *fileFuncs;

	fileFuncs = RwOsGetFileInterface();
	return (int)fileFuncs->rwfclose((void*)fd);
}

bool
CFileMgr::GetCdFile(const char *file, uint32 &pos, uint32 &size)
{
	sceCdlFILE cdfile;
	if(sceCdSearchFile(&cdfile, file) == 0)
		return false;
	pos = cdfile.lsn;
	size = cdfile.size;
	return true;
}

void
CFileMgr::InitCd(void)
{
	sceCdDiskReady(0);
	sceCdStandby();
	sceCdSync(0);
}

void
CFileMgr::InitCdSystem(void)
{
	sceCdInit(SCECdINIT);
	sceCdMmode(SCECdDVD);
}
