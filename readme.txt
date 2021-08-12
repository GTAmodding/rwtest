To build, install Sony SDK and RW 3.1 SDK. check options.mk for RW paths
Make sure you check the defines at the top of main.cpp.

To make a disc, check disc.iml and add the necessary files to the disc directory,
then use iml2iso to make an iso.
Take gta3.dir/img from ps2 gta3, IOPRP300.IMG, SIO2MAN.IRX and PADMAN.IRX from the Sony SDK.
CDSTREAM.IRX you can build yourself or you use the provided one.

