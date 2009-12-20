/* Bits and pieces from sanei */

#ifndef sanei_h
#define sanei_h

#include <sane/sane.h>

SANE_Status sanei_constrain_value(const SANE_Option_Descriptor * opt,
				  void * value, SANE_Word * info);
const char *sanei_libusb_strerror(SANE_Status errcode);
const char *sanei_strerror(SANE_Status errcode);

#endif /* sanei_h */

