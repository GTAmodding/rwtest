#if (!defined(SKYFS_H))
#define SKYFS_H

#include <rwcore.h>

#ifdef    __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

RwBool SkyInstallFileSystem(void);
void SkySetDirectory(const char *dir);
void SkyRegisterFileOnCd(const char *file);

#ifdef    __cplusplus
}
#endif                          /* __cplusplus */


#endif /* (!defined(SKYFS_H)) */
