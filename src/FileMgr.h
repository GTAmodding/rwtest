class CFileMgr
{
	static char ms_rootDirName[128];
	static char ms_dirName[128];
public:
	static void Initialise(void);
	static void ChangeDir(const char *dir);
	static void SetDir(const char *dir);
// not on PS2
	static void SetDirMyDocuments(void);
	static ssize_t LoadFile(const char *file, uint8 *buf, int maxlen, const char *mode);
// not on PS2
	static int OpenFile(const char *file, const char *mode);
	static int OpenFile(const char *file);
	static int OpenFileForWriting(const char *file);
	static size_t Read(int fd, char *buf, ssize_t len);
	static size_t Write(int fd, const char *buf, ssize_t len);
	static bool Seek(int fd, int offset, int whence);
	static bool ReadLine(int fd, char *buf, int len);
	static int CloseFile(int fd);
// not on PS2
	static int GetErrorReadWrite(int fd);
// not on PS2
	static char *GetRootDirName() { return ms_rootDirName; }

// only PS2
	static bool GetCdFile(const char *file, uint32 &size, uint32 &offset);
	static void InitCd(void);
	static void InitCdSystem(void);
};

