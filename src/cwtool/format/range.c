/****************************************************************************
 ****************************************************************************
 *
 * format/range.c
 *
 ****************************************************************************
 ****************************************************************************/





#include <stdio.h>

#include "range.h"
#include "../error.h"
#include "../debug.h"
#include "../verbose.h"
#include "../global.h"
#include "../options.h"




/****************************************************************************
 *
 * global functions
 *
 ****************************************************************************/




/****************************************************************************
 * range_sector_header
 ****************************************************************************/
struct range *
range_sector_header(
	struct range_sector		*rng_sec)

	{
	return (&rng_sec->rng_header);
	}



/****************************************************************************
 * range_sector_data
 ****************************************************************************/
struct range *
range_sector_data(
	struct range_sector		*rng_sec)

	{
	return (&rng_sec->rng_data);
	}



/****************************************************************************
 * range_sector_set_number
 ****************************************************************************/
cw_void_t
range_sector_set_number(
	struct range_sector		*rng_sec,
	cw_count_t			number)

	{
	rng_sec->number = number;
	}



/****************************************************************************
 * range_sector_get_number
 ****************************************************************************/
cw_count_t
range_sector_get_number(
	struct range_sector		*rng_sec)

	{
	return (rng_sec->number);
	}



/****************************************************************************
 * range_set_start
 ****************************************************************************/
cw_void_t
range_set_start(
	struct range			*rng,
	cw_count_t			start)

	{
	rng->start = start;
	}



/****************************************************************************
 * range_set_end
 ****************************************************************************/
cw_void_t
range_set_end(
	struct range			*rng,
	cw_count_t			end)

	{
	rng->end = end;
	}



/****************************************************************************
 * range_get_start
 ****************************************************************************/
cw_count_t
range_get_start(
	struct range			*rng)

	{
	return (rng->start);
	}



/****************************************************************************
 * range_get_end
 ****************************************************************************/
cw_count_t
range_get_end(
	struct range			*rng)

	{
	return (rng->end);
	}
/******************************************************** Karsten Scheibler */
