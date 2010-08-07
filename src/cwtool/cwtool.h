/****************************************************************************
 ****************************************************************************
 *
 * cwtool.h
 *
 ****************************************************************************
 ****************************************************************************/





#ifndef CWTOOL_H
#define CWTOOL_H

#include "ioctl.h"

#define CWTOOL_MAX_NAME_LEN		64
#define CWTOOL_MIN_TRACK_SIZE		1024
#define CWTOOL_MAX_TRACK_SIZE		(CW_MAX_SIZE - CW_WRITE_OVERHEAD)
#define CWTOOL_MAX_TRACK		(2 * CW_MAX_TRACK)
#define CWTOOL_MAX_SECTOR		128
#define CWTOOL_MAX_DISK			64



#endif /* !CWTOOL_H */
/******************************************************** Karsten Scheibler */