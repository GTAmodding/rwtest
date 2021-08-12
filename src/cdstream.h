#ifdef    __cplusplus
extern "C"
{
#endif


void CdStreamInit(int n);
int CdStreamRead(int chan, void *buf, unsigned int off, unsigned int size);
int CdStreamGetStatus(int chan);
int CdStreamGetLastPosn(void);
int CdStreamSync(int chan);

#ifdef    __cplusplus
}
#endif

