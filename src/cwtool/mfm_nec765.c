/****************************************************************************
 ****************************************************************************
 *
 * mfm_nec765.c
 *
 ****************************************************************************
 ****************************************************************************/





#include <stdio.h>

#include "mfm_nec765.h"
#include "error.h"
#include "debug.h"
#include "verbose.h"
#include "cwtool.h"
#include "disk.h"
#include "fifo.h"
#include "format.h"
#include "mfm.h"
#include "raw.h"
#include "setvalue.h"




/****************************************************************************
 *
 * functions for sector and track handling
 *
 ****************************************************************************/




#define HEADER_SIZE			7
#define DATA_SIZE			16387
#define FLAG_RD_IGNORE_SECTOR_SIZE	(1 << 0)
#define FLAG_RD_IGNORE_CHECKSUMS	(1 << 1)
#define FLAG_RD_IGNORE_TRACK_MISMATCH	(1 << 2)
#define FLAG_RD_IGNORE_FORMAT_BYTE	(1 << 3)
#define FLAG_RW_CRC16_INIT_VALUE_SET	(1 << 4)



/****************************************************************************
 * mfm_nec765_sector_shift
 ****************************************************************************/
static int
mfm_nec765_sector_shift(
	struct mfm_nec765		*mfm_nec,
	int				sector)

	{
	return (mfm_get_sector_shift(mfm_nec->rw.pshift, sector, CWTOOL_MAX_SECTOR));
	}



/****************************************************************************
 * mfm_nec765_sector_size
 ****************************************************************************/
static int
mfm_nec765_sector_size(
	struct mfm_nec765		*mfm_nec,
	int				sector)

	{
	int				shift = mfm_nec765_sector_shift(mfm_nec, sector);

	if (shift == -1) shift = 2;
	return (0x80 << shift);
	}



/****************************************************************************
 * mfm_nec765_read_sector2
 ****************************************************************************/
static int
mfm_nec765_read_sector2(
	struct fifo			*ffo_l1,
	struct mfm_nec765		*mfm_nec,
	struct disk_error		*dsk_err,
	unsigned char			*header,
	unsigned char			*data)

	{
	int				bitofs, data_size;

	while (1)
		{
		*dsk_err = (struct disk_error) { };
		if (mfm_read_sync(ffo_l1, mfm_nec->rw.sync_value, mfm_nec->rw.sync_length) == -1) return (-1);
		bitofs = fifo_get_rd_bitofs(ffo_l1);
		if (mfm_read_bytes(ffo_l1, dsk_err, header, HEADER_SIZE) == -1) return (-1);
		fifo_set_rd_bitofs(ffo_l1, bitofs);
		if (format_compare2("id_address_mark: got 0x%02x, expected 0x%02x", header[0], mfm_nec->rw.id_address_mark) == 0) break;
		}
	data_size = mfm_nec765_sector_size(mfm_nec, header[3] - 1);
	if (mfm_read_sync(ffo_l1, mfm_nec->rw.sync_value, mfm_nec->rw.sync_length) == -1) return (-1);
	if (mfm_read_bytes(ffo_l1, dsk_err, data, data_size + 3) == -1) return (-1);
	verbose(2, "rewinding to bit offset %d", bitofs);
	fifo_set_rd_bitofs(ffo_l1, bitofs);
	return (1);
	}



/****************************************************************************
 * mfm_nec765_write_sector2
 ****************************************************************************/
static int
mfm_nec765_write_sector2(
	struct fifo			*ffo_l1,
	struct mfm_nec765		*mfm_nec,
	unsigned char			*header,
	unsigned char			*data,
	int				data_size)

	{
	if (mfm_write_fill(ffo_l1, mfm_nec->wr.fill_value2, mfm_nec->wr.fill_length2) == -1) return (-1);
	if (mfm_write_fill(ffo_l1, mfm_nec->wr.fill_value3, mfm_nec->wr.fill_length3) == -1) return (-1);
	if (mfm_write_sync(ffo_l1, mfm_nec->rw.sync_value, mfm_nec->rw.sync_length) == -1) return (-1);
	if (mfm_write_bytes(ffo_l1, header, HEADER_SIZE) == -1) return (-1);
	if (mfm_write_fill(ffo_l1, mfm_nec->wr.fill_value4, mfm_nec->wr.fill_length4) == -1) return (-1);
	if (mfm_write_fill(ffo_l1, mfm_nec->wr.fill_value5, mfm_nec->wr.fill_length5) == -1) return (-1);
	if (mfm_write_sync(ffo_l1, mfm_nec->rw.sync_value, mfm_nec->rw.sync_length) == -1) return (-1);
	if (mfm_write_bytes(ffo_l1, data, data_size) == -1) return (-1);
	return (1);
	}



/****************************************************************************
 * mfm_nec765_read_sector
 ****************************************************************************/
static int
mfm_nec765_read_sector(
	struct fifo			*ffo_l1,
	struct mfm_nec765		*mfm_nec,
	struct disk_sector		*dsk_sct,
	int				track)

	{
	struct disk_error		dsk_err;
	unsigned char			header[HEADER_SIZE];
	unsigned char			data[DATA_SIZE];
	int				result, sector, data_size;

	if (mfm_nec765_read_sector2(ffo_l1, mfm_nec, &dsk_err, header, data) == -1) return (-1);

	/* accept only valid sector numbers */

	sector = header[3] - 1;
	if ((sector < 0) || (sector >= mfm_nec->rw.sectors))
		{
		verbose(1, "sector %d out of range", sector);
		return (0);
		}
	verbose(1, "got sector %d", sector);

	/* check sector quality */

	data_size = mfm_nec765_sector_size(mfm_nec, sector);
	result = format_compare2("sector size: got %d, expected %d", 1 << (header[4] + 7), data_size);
	if (result > 0) verbose(2, "wrong sector size on sector %d", sector);
	if (mfm_nec->rd.flags & FLAG_RD_IGNORE_SECTOR_SIZE) dsk_err.warnings += result;
	else dsk_err.errors += result;

	result = format_compare2("header crc16 checksum: got 0x%04x, expected 0x%04x", mfm_read_ushort_be(&header[5]), mfm_crc16(mfm_nec->rw.crc16_init_value, header, 5));
	result += format_compare2("data crc16 checksum: got 0x%04x, expected 0x%04x", mfm_read_ushort_be(&data[data_size + 1]), mfm_crc16(mfm_nec->rw.crc16_init_value, data, data_size + 1));
	if (result > 0) verbose(2, "checksum error on sector %d", sector);
	if (mfm_nec->rd.flags & FLAG_RD_IGNORE_CHECKSUMS) dsk_err.warnings += result;
	else dsk_err.errors += result;

	result = format_compare2("track: got %d, expected %d", header[1], track / 2);
	result += format_compare2("side: got %d, expected %d", header[2], track % 2);
	if (result > 0) verbose(2, "track or side mismatch on sector %d", sector);
	if (mfm_nec->rd.flags & FLAG_RD_IGNORE_TRACK_MISMATCH) dsk_err.warnings += result;
	else dsk_err.errors += result;

	result = format_compare3("data_address_mark: got 0x%02x, expected 0x%02x or 0x%02x", data[0], mfm_nec->rw.data_address_mark1, mfm_nec->rw.data_address_mark2);
	if (result > 0) verbose(2, "wrong data_address_mark on sector %d", sector);
	if (mfm_nec->rd.flags & FLAG_RD_IGNORE_FORMAT_BYTE) dsk_err.warnings += result;
	else dsk_err.errors += result;

	/*
	 * if the found sector is of better quality than the current one
	 * then take the data
	 */

	disk_set_sector_number(&dsk_sct[sector], sector);
	disk_sector_read(&dsk_sct[sector], &dsk_err, &data[1]);
	return (1);
	}



/****************************************************************************
 * mfm_nec765_write_sector
 ****************************************************************************/
static int
mfm_nec765_write_sector(
	struct fifo			*ffo_l1,
	struct mfm_nec765		*mfm_nec,
	struct disk_sector		*dsk_sct,
	int				track)

	{
	unsigned char			header[HEADER_SIZE];
	unsigned char			data[DATA_SIZE];
	int				sector = disk_get_sector_number(dsk_sct);
	int				data_size;

	verbose(1, "writing sector %d", sector);
	header[0] = mfm_nec->rw.id_address_mark;
	header[1] = track / 2;
	header[2] = track % 2;
	header[3] = sector + 1;
	header[4] = mfm_nec765_sector_shift(mfm_nec, sector);
	data[0]   = mfm_nec->rw.data_address_mark1;
	data_size = mfm_nec765_sector_size(mfm_nec, sector);
	mfm_write_ushort_be(&header[5], mfm_crc16(mfm_nec->rw.crc16_init_value, header, 5));
	disk_sector_write(&data[1], dsk_sct);
	mfm_write_ushort_be(&data[data_size + 1], mfm_crc16(mfm_nec->rw.crc16_init_value, data, data_size + 1));
	return (mfm_nec765_write_sector2(ffo_l1, mfm_nec, header, data, data_size + 3));
	}



/****************************************************************************
 * mfm_nec765_statistics
 ****************************************************************************/
static int
mfm_nec765_statistics(
	union format			*fmt,
	struct fifo			*ffo_l0,
	int				track)

	{
	raw_histogram(ffo_l0, track);
	raw_precomp_statistics(ffo_l0, fmt->mfm_nec.rw.bnd, 3);
	return (1);
	}



/****************************************************************************
 * mfm_nec765_read_track
 ****************************************************************************/
static int
mfm_nec765_read_track(
	union format			*fmt,
	struct fifo			*ffo_l0,
	struct fifo			*ffo_l3,
	struct disk_sector		*dsk_sct,
	int				track)

	{
	unsigned char			data[CWTOOL_MAX_TRACK_SIZE];
	struct fifo			ffo_l1 = FIFO_INIT(data, sizeof (data));

	raw_read(ffo_l0, &ffo_l1, fmt->mfm_nec.rw.bnd, 3);
	while (mfm_nec765_read_sector(&ffo_l1, &fmt->mfm_nec, dsk_sct, track) != -1) ;
	return (1);
	}



/****************************************************************************
 * mfm_nec765_write_track
 ****************************************************************************/
static int
mfm_nec765_write_track(
	union format			*fmt,
	struct fifo			*ffo_l3,
	struct disk_sector		*dsk_sct,
	struct fifo			*ffo_l0,
	int				track)

	{
	unsigned char			data[CWTOOL_MAX_TRACK_SIZE];
	struct fifo			ffo_l1 = FIFO_INIT(data, sizeof (data));
	int				i;

	if (mfm_write_fill(&ffo_l1, fmt->mfm_nec.wr.prolog_value, fmt->mfm_nec.wr.prolog_length) == -1) return (0);
	if (mfm_write_fill(&ffo_l1, fmt->mfm_nec.wr.fill_value1, fmt->mfm_nec.wr.fill_length1)   == -1) return (0);
	if (mfm_write_sync(&ffo_l1, 0x5224, 3) == -1) return (0);
	if (mfm_write_fill(&ffo_l1, 0xfc, 1)   == -1) return (0);
	for (i = 0; i < fmt->mfm_nec.rw.sectors; i++) if (mfm_nec765_write_sector(&ffo_l1, &fmt->mfm_nec, &dsk_sct[i], track) == -1) return (0);
	if (mfm_write_fill(&ffo_l1, fmt->mfm_nec.wr.fill_value6, fmt->mfm_nec.wr.fill_length6)   == -1) return (0);
	if (mfm_write_fill(&ffo_l1, fmt->mfm_nec.wr.fill_value7, fmt->mfm_nec.wr.fill_length7)   == -1) return (0);
	if (mfm_write_sync(&ffo_l1, 0x4489, 3) == -1) return (0);
	if (mfm_write_fill(&ffo_l1, fmt->mfm_nec.wr.epilog_value, fmt->mfm_nec.wr.epilog_length) == -1) return (0);
	fifo_write_flush(&ffo_l1);
	if (raw_write(&ffo_l1, ffo_l0, fmt->mfm_nec.rw.bnd, fmt->mfm_nec.wr.precomp, 3) == -1) return (0);
	return (1);
	}




/****************************************************************************
 *
 * functions for configuration
 *
 ****************************************************************************/




#define MAGIC_IGNORE_SECTOR_SIZE	1
#define MAGIC_IGNORE_CHECKSUMS		2
#define MAGIC_IGNORE_TRACK_MISMATCH	3
#define MAGIC_IGNORE_FORMAT_BYTE	4
#define MAGIC_PROLOG_LENGTH		5
#define MAGIC_PROLOG_VALUE		6
#define MAGIC_EPILOG_LENGTH		7
#define MAGIC_EPILOG_VALUE		8
#define MAGIC_FILL_LENGTH1		9
#define MAGIC_FILL_VALUE1		10
#define MAGIC_FILL_LENGTH2		11
#define MAGIC_FILL_VALUE2		12
#define MAGIC_FILL_LENGTH3		13
#define MAGIC_FILL_VALUE3		14
#define MAGIC_FILL_LENGTH4		15
#define MAGIC_FILL_VALUE4		16
#define MAGIC_FILL_LENGTH5		17
#define MAGIC_FILL_VALUE5		18
#define MAGIC_FILL_LENGTH6		19
#define MAGIC_FILL_VALUE6		20
#define MAGIC_FILL_LENGTH7		21
#define MAGIC_FILL_VALUE7		22
#define MAGIC_PRECOMP			23
#define MAGIC_SECTORS			24
#define MAGIC_SYNC_LENGTH		25
#define MAGIC_SYNC_VALUE		26
#define MAGIC_CRC16_INIT_VALUE		27
#define MAGIC_ID_ADDRESS_MARK		28
#define MAGIC_DATA_ADDRESS_MARK1	29
#define MAGIC_DATA_ADDRESS_MARK2	30
#define MAGIC_SECTOR_SIZES		31
#define MAGIC_BOUNDS			32



/****************************************************************************
 * mfm_nec765_set_crc16_init_value
 ****************************************************************************/
static int
mfm_nec765_set_crc16_init_value(
	struct mfm_nec765		*mfm_nec)

	{
	unsigned char			data[256];
	int				d, i;

	if (mfm_nec->rw.flags & FLAG_RW_CRC16_INIT_VALUE_SET) return (1);
	for (d = 0, i = 0x4000; i > 0; i >>= 2) d = (d << 1) | ((mfm_nec->rw.sync_value & i) ? 1 : 0);
	for (i = 0; i < mfm_nec->rw.sync_length; i++) data[i] = d;
	mfm_nec->rw.crc16_init_value = mfm_crc16(0xffff, data, i);
	debug(2, "calculated crc16_init_value = 0x%04x", mfm_nec->rw.crc16_init_value);
	return (1);
	}



/****************************************************************************
 * mfm_nec765_set_defaults
 ****************************************************************************/
static void
mfm_nec765_set_defaults(
	union format			*fmt)

	{
	const static struct mfm_nec765	mfm_nec =
		{
		.rd =
			{
			.flags = 0
			},
		.wr =
			{
			.prolog_length = 80,
			.epilog_length = 652,
			.prolog_value  = 0x4e,
			.epilog_value  = 0x4e,
			.fill_length1  = 12,
			.fill_value1   = 0x00,
			.fill_length2  = 50,
			.fill_value2   = 0x4e,
			.fill_length3  = 12,
			.fill_value3   = 0x00,
			.fill_length4  = 22,
			.fill_value4   = 0x4e,
			.fill_length5  = 12,
			.fill_value5   = 0x00,
			.fill_length6  = 54,
			.fill_value6   = 0x4e,
			.fill_length7  = 12,
			.fill_value7   = 0x00,
			.precomp       = { }
			},
		.rw =
			{
			.sectors            = 9,
			.sync_length        = 3,
			.sync_value         = 0x4489,
			.crc16_init_value   = 0,
			.flags              = 0,
			.id_address_mark    = 0xfe,
			.data_address_mark1 = 0xfb,
			.data_address_mark2 = 0xf8,
			.bnd                =
				{
				BOUNDS(0x0800, 0x1b52, 0x2300, 1),
				BOUNDS(0x2400, 0x297c, 0x3000, 2),
				BOUNDS(0x3100, 0x37a5, 0x4000, 3)
				}
			}
		};

	debug(2, "setting defaults");
	fmt->mfm_nec = mfm_nec;
	mfm_nec765_set_crc16_init_value(&fmt->mfm_nec);
	mfm_fill_sector_shift(fmt->mfm_nec.rw.pshift, 0, CWTOOL_MAX_SECTOR, 2);
	}



/****************************************************************************
 * mfm_nec765_set_read_option
 ****************************************************************************/
static int
mfm_nec765_set_read_option(
	union format			*fmt,
	int				magic,
	int				val,
	int				ofs)

	{
	debug(2, "setting read option magic = %d, val = %d, ofs = %d", magic, val, ofs);
	if (magic == MAGIC_IGNORE_SECTOR_SIZE)    return (setvalue_uchar_bit(&fmt->mfm_nec.rd.flags, val, FLAG_RD_IGNORE_SECTOR_SIZE));
	if (magic == MAGIC_IGNORE_CHECKSUMS)      return (setvalue_uchar_bit(&fmt->mfm_nec.rd.flags, val, FLAG_RD_IGNORE_CHECKSUMS));
	if (magic == MAGIC_IGNORE_TRACK_MISMATCH) return (setvalue_uchar_bit(&fmt->mfm_nec.rd.flags, val, FLAG_RD_IGNORE_TRACK_MISMATCH));
	debug_error_condition(magic != MAGIC_IGNORE_FORMAT_BYTE);
	return (setvalue_uchar_bit(&fmt->mfm_nec.rd.flags, val, FLAG_RD_IGNORE_FORMAT_BYTE));
	}



/****************************************************************************
 * mfm_nec765_set_write_option
 ****************************************************************************/
static int
mfm_nec765_set_write_option(
	union format			*fmt,
	int				magic,
	int				val,
	int				ofs)

	{
	debug(2, "setting write option magic = %d, val = %d, ofs = %d", magic, val, ofs);
	if (magic == MAGIC_PROLOG_LENGTH) return (setvalue_ushort(&fmt->mfm_nec.wr.prolog_length, val, 1, 0xffff));
	if (magic == MAGIC_PROLOG_VALUE)  return (setvalue_uchar(&fmt->mfm_nec.wr.prolog_value, val, 0, 0xff));
	if (magic == MAGIC_EPILOG_LENGTH) return (setvalue_ushort(&fmt->mfm_nec.wr.epilog_length, val, 1, 0xffff));
	if (magic == MAGIC_EPILOG_VALUE)  return (setvalue_uchar(&fmt->mfm_nec.wr.epilog_value, val, 0, 0xff));
	if (magic == MAGIC_FILL_LENGTH1)  return (setvalue_uchar(&fmt->mfm_nec.wr.fill_length1, val, 0, 0xff));
	if (magic == MAGIC_FILL_VALUE1)   return (setvalue_uchar(&fmt->mfm_nec.wr.fill_value1, val, 0, 0xff));
	if (magic == MAGIC_FILL_LENGTH2)  return (setvalue_uchar(&fmt->mfm_nec.wr.fill_length2, val, 0, 0xff));
	if (magic == MAGIC_FILL_VALUE2)   return (setvalue_uchar(&fmt->mfm_nec.wr.fill_value2, val, 0, 0xff));
	if (magic == MAGIC_FILL_LENGTH3)  return (setvalue_uchar(&fmt->mfm_nec.wr.fill_length3, val, 0, 0xff));
	if (magic == MAGIC_FILL_VALUE3)   return (setvalue_uchar(&fmt->mfm_nec.wr.fill_value3, val, 0, 0xff));
	if (magic == MAGIC_FILL_LENGTH4)  return (setvalue_uchar(&fmt->mfm_nec.wr.fill_length4, val, 0, 0xff));
	if (magic == MAGIC_FILL_VALUE4)   return (setvalue_uchar(&fmt->mfm_nec.wr.fill_value4, val, 0, 0xff));
	if (magic == MAGIC_FILL_LENGTH5)  return (setvalue_uchar(&fmt->mfm_nec.wr.fill_length5, val, 0, 0xff));
	if (magic == MAGIC_FILL_VALUE5)   return (setvalue_uchar(&fmt->mfm_nec.wr.fill_value5, val, 0, 0xff));
	if (magic == MAGIC_FILL_LENGTH6)  return (setvalue_uchar(&fmt->mfm_nec.wr.fill_length6, val, 0, 0xff));
	if (magic == MAGIC_FILL_VALUE6)   return (setvalue_uchar(&fmt->mfm_nec.wr.fill_value6, val, 0, 0xff));
	if (magic == MAGIC_FILL_LENGTH7)  return (setvalue_uchar(&fmt->mfm_nec.wr.fill_length7, val, 0, 0xff));
	if (magic == MAGIC_FILL_VALUE7)   return (setvalue_uchar(&fmt->mfm_nec.wr.fill_value7, val, 0, 0xff));
	debug_error_condition(magic != MAGIC_PRECOMP);
	return (setvalue_short(&fmt->mfm_nec.wr.precomp[ofs], val, -0x4000, 0x4000));
	}



/****************************************************************************
 * mfm_nec765_set_rw_option
 ****************************************************************************/
static int
mfm_nec765_set_rw_option(
	union format			*fmt,
	int				magic,
	int				val,
	int				ofs)

	{
	debug(2, "setting rw option magic = %d, val = %d, ofs = %d", magic, val, ofs);
	if (magic == MAGIC_SECTORS)
		{
		mfm_fill_sector_shift(fmt->mfm_nec.rw.pshift, fmt->mfm_nec.rw.sectors, CWTOOL_MAX_SECTOR, 2);
		return (setvalue_uchar(&fmt->mfm_nec.rw.sectors, val, 1, CWTOOL_MAX_SECTOR));
		}
	if (magic == MAGIC_SYNC_LENGTH)
		{
		if (! setvalue_uchar(&fmt->mfm_nec.rw.sync_length, val, 0, 0xff)) return (0);
		return (mfm_nec765_set_crc16_init_value(&fmt->mfm_nec));
		}
	if (magic == MAGIC_SYNC_VALUE)
		{
		if (! setvalue_ushort(&fmt->mfm_nec.rw.sync_value, val, 0, 0xffff)) return (0);
		return (mfm_nec765_set_crc16_init_value(&fmt->mfm_nec));
		}
	if (magic == MAGIC_CRC16_INIT_VALUE)
		{
		setvalue_uchar_bit(&fmt->mfm_nec.rw.flags, (val < 0) ? 0 : 1, FLAG_RW_CRC16_INIT_VALUE_SET);
		if (val < 0) return (mfm_nec765_set_crc16_init_value(&fmt->mfm_nec));
		return (setvalue_ushort(&fmt->mfm_nec.rw.crc16_init_value, val, 0, 0xffff));
		}
	if (magic == MAGIC_ID_ADDRESS_MARK)    return (setvalue_uchar(&fmt->mfm_nec.rw.id_address_mark, val, 0, 0xff));
	if (magic == MAGIC_DATA_ADDRESS_MARK1) return (setvalue_uchar(&fmt->mfm_nec.rw.data_address_mark1, val, 0, 0xff));
	if (magic == MAGIC_DATA_ADDRESS_MARK2) return (setvalue_uchar(&fmt->mfm_nec.rw.data_address_mark2, val, 0, 0xff));
	if (magic == MAGIC_SECTOR_SIZES)       return (mfm_set_sector_size(fmt->mfm_nec.rw.pshift, ofs, CWTOOL_MAX_SECTOR, val));
	debug_error_condition(magic != MAGIC_BOUNDS);
	return (setvalue_bounds(fmt->mfm_nec.rw.bnd, val, ofs));
	}



/****************************************************************************
 * mfm_nec765_get_sector_size
 ****************************************************************************/
static int
mfm_nec765_get_sector_size(
	union format			*fmt,
	int				sector)

	{
	int				i, s;

	debug_error_condition(sector >= fmt->mfm_nec.rw.sectors);
	if (sector >= 0) return (mfm_nec765_sector_size(&fmt->mfm_nec, sector));
	for (i = s = 0; i < fmt->mfm_nec.rw.sectors; i++) s += mfm_nec765_sector_size(&fmt->mfm_nec, i);
	return (s);
	}



/****************************************************************************
 * mfm_nec765_get_sectors
 ****************************************************************************/
static int
mfm_nec765_get_sectors(
	union format			*fmt)

	{
	return (fmt->mfm_nec.rw.sectors);
	}



/****************************************************************************
 * mfm_nec765_read_options
 ****************************************************************************/
static struct format_option		mfm_nec765_read_options[] =
	{
	FORMAT_OPTION("ignore_sector_size",    MAGIC_IGNORE_SECTOR_SIZE,    1),
	FORMAT_OPTION("ignore_checksums",      MAGIC_IGNORE_CHECKSUMS,      1),
	FORMAT_OPTION("ignore_track_mismatch", MAGIC_IGNORE_TRACK_MISMATCH, 1),
	FORMAT_OPTION("ignore_format_byte",    MAGIC_IGNORE_FORMAT_BYTE,    1),
	FORMAT_OPTION(NULL, 0, 0)
	};



/****************************************************************************
 * mfm_nec765_write_options
 ****************************************************************************/
static struct format_option		mfm_nec765_write_options[] =
	{
	FORMAT_OPTION("prolog_length", MAGIC_PROLOG_LENGTH, 1),
	FORMAT_OPTION("prolog_value",  MAGIC_PROLOG_VALUE,  1),
	FORMAT_OPTION("epilog_length", MAGIC_EPILOG_LENGTH, 1),
	FORMAT_OPTION("epilog_value",  MAGIC_EPILOG_VALUE,  1),
	FORMAT_OPTION("fill_length1",  MAGIC_FILL_LENGTH1,  1),
	FORMAT_OPTION("fill_value1",   MAGIC_FILL_VALUE1,   1),
	FORMAT_OPTION("fill_length2",  MAGIC_FILL_LENGTH2,  1),
	FORMAT_OPTION("fill_value2",   MAGIC_FILL_VALUE2,   1),
	FORMAT_OPTION("fill_length3",  MAGIC_FILL_LENGTH3,  1),
	FORMAT_OPTION("fill_value3",   MAGIC_FILL_VALUE3,   1),
	FORMAT_OPTION("fill_length4",  MAGIC_FILL_LENGTH4,  1),
	FORMAT_OPTION("fill_value4",   MAGIC_FILL_VALUE4,   1),
	FORMAT_OPTION("fill_length5",  MAGIC_FILL_LENGTH5,  1),
	FORMAT_OPTION("fill_value5",   MAGIC_FILL_VALUE5,   1),
	FORMAT_OPTION("fill_length6",  MAGIC_FILL_LENGTH6,  1),
	FORMAT_OPTION("fill_value6",   MAGIC_FILL_VALUE6,   1),
	FORMAT_OPTION("fill_length7",  MAGIC_FILL_LENGTH7,  1),
	FORMAT_OPTION("fill_value7",   MAGIC_FILL_VALUE7,   1),
	FORMAT_OPTION("precomp",       MAGIC_PRECOMP,       9),
	FORMAT_OPTION(NULL, 0, 0)
	};



/****************************************************************************
 * mfm_nec765_rw_options
 ****************************************************************************/
static struct format_option		mfm_nec765_rw_options[] =
	{
	FORMAT_OPTION("sectors",            MAGIC_SECTORS,             1),
	FORMAT_OPTION("sync_length",        MAGIC_SYNC_LENGTH,         1),
	FORMAT_OPTION("sync_value",         MAGIC_SYNC_LENGTH,         1),
	FORMAT_OPTION("crc16_init_value",   MAGIC_CRC16_INIT_VALUE,    1),
	FORMAT_OPTION("id_address_mark",    MAGIC_ID_ADDRESS_MARK,     1),
	FORMAT_OPTION("data_address_mark1", MAGIC_DATA_ADDRESS_MARK1,  1),
	FORMAT_OPTION("data_address_mark2", MAGIC_DATA_ADDRESS_MARK2,  1),
	FORMAT_OPTION("sector_sizes",       MAGIC_SECTOR_SIZES,       -1),
	FORMAT_OPTION("bounds",             MAGIC_BOUNDS,              9),
	FORMAT_OPTION(NULL, 0, 0)
	};




/****************************************************************************
 *
 * used by external callers
 *
 ****************************************************************************/




/****************************************************************************
 * mfm_nec765_format_desc
 ****************************************************************************/
struct format_desc			mfm_nec765_format_desc =
	{
	.name             = "mfm_nec765",
	.level            = 3,
	.set_defaults     = mfm_nec765_set_defaults,
	.set_read_option  = mfm_nec765_set_read_option,
	.set_write_option = mfm_nec765_set_write_option,
	.set_rw_option    = mfm_nec765_set_rw_option,
	.get_sectors      = mfm_nec765_get_sectors,
	.get_sector_size  = mfm_nec765_get_sector_size,
	.track_statistics = mfm_nec765_statistics,
	.track_read       = mfm_nec765_read_track,
	.track_write      = mfm_nec765_write_track,
	.fmt_opt_rd       = mfm_nec765_read_options,
	.fmt_opt_wr       = mfm_nec765_write_options,
	.fmt_opt_rw       = mfm_nec765_rw_options
	};
/******************************************************** Karsten Scheibler */
