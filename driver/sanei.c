/* sane - Scanner Access Now Easy.

   sanei_constrain_value(), sanei_strerror()
   Copyright (C) 1996, 1997 David Mosberger-Tang and Andreas Beck

   sanei_libusb_strerror()
   Copyright (C) 2009 Julien BLACHE <jb@jblache.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.  */

/* Bits and pieces from sanei */

#include <stdlib.h>
#include <string.h>
#include <sane/sane.h>
#include <libusb-1.0/libusb.h>

const char *sanei_libusb_strerror(SANE_Status errcode)
{
	switch (errcode) {
	case LIBUSB_SUCCESS:
		return "Success (no error)";
	case LIBUSB_ERROR_IO:
		return "Input/output error";
	case LIBUSB_ERROR_INVALID_PARAM:
		return "Invalid parameter";
	case LIBUSB_ERROR_ACCESS:
		return "Access denied (insufficient permissions)";
	case LIBUSB_ERROR_NO_DEVICE:
		return "No such device (it may have been disconnected)";
	case LIBUSB_ERROR_NOT_FOUND:
		return "Entity not found";
	case LIBUSB_ERROR_BUSY:
		return "Resource busy";
	case LIBUSB_ERROR_TIMEOUT:
		return "Operation timed out";
	case LIBUSB_ERROR_OVERFLOW:
		return "Overflow";
	case LIBUSB_ERROR_PIPE:
		return "Pipe error";
	case LIBUSB_ERROR_INTERRUPTED:
		return "System call interrupted (perhaps due to signal)";
	case LIBUSB_ERROR_NO_MEM:
		return "Insufficient memory";
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return "Operation not supported or unimplemented on this platform";
	case LIBUSB_ERROR_OTHER:
		return "Other error";
	default:
		return "Unknown libusb-1.0 error code";
	}
}

const char *sanei_strerror(SANE_Status errcode)
{
	switch (errcode) {
	case SANE_STATUS_GOOD:
		return "everything A-OK";
	case SANE_STATUS_UNSUPPORTED:
		return "operation is not supported";
	case SANE_STATUS_CANCELLED:
		return "operation was cancelled";
	case SANE_STATUS_DEVICE_BUSY:
		return "device is busy; try again later";
	case SANE_STATUS_INVAL:
		return "data is invalid (includes no dev at open)";
	case SANE_STATUS_EOF:
		return "no more data available (end-of-file)";
	case SANE_STATUS_JAMMED:
		return "document feeder jammed";
	case SANE_STATUS_NO_DOCS:
		return "document feeder out of documents";
	case SANE_STATUS_COVER_OPEN:
		return "scanner cover is open";
	case SANE_STATUS_IO_ERROR:
		return "error during device I/O";
	case SANE_STATUS_NO_MEM:
		return "out of memory";
	case SANE_STATUS_ACCESS_DENIED:
		return "access to resource has been denied";
	default:
		return "undefined SANE error";
	}
}

/**
 * This function apply the constraint defined by the option descriptor
 * to the given value, and update the info flags holder if needed. It
 * return SANE_STATUS_INVAL if the constraint cannot be applied, else
 * it returns SANE_STATUS_GOOD.
 */
SANE_Status
sanei_constrain_value (const SANE_Option_Descriptor * opt, void *value,
		       SANE_Word * info)
{
  const SANE_String_Const *string_list;
  const SANE_Word *word_list;
  int i, k, num_matches, match;
  const SANE_Range *range;
  SANE_Word w, v, *array;
  SANE_Bool b;
  size_t len;

  switch (opt->constraint_type)
    {
    case SANE_CONSTRAINT_RANGE:

      /* single values are treated as arrays of length 1 */
      array = (SANE_Word *) value;

      /* compute number of elements */
      if (opt->size > 0)
	{
	  k = opt->size / sizeof (SANE_Word);
	}
      else
	{
	  k = 1;
	}

      range = opt->constraint.range;
      /* for each element of the array, we apply the constraint */
      for (i = 0; i < k; i++)
	{
	  /* constrain min */
	  if (array[i] < range->min)
	    {
	      array[i] = range->min;
	      if (info)
		{
		  *info |= SANE_INFO_INEXACT;
		}
	    }

	  /* constrain max */
	  if (array[i] > range->max)
	    {
	      array[i] = range->max;
	      if (info)
		{
		  *info |= SANE_INFO_INEXACT;
		}
	    }

	  /* quantization */
	  if (range->quant)
	    {
	      v =
		(unsigned int) (array[i] - range->min +
				range->quant / 2) / range->quant;
	      v = v * range->quant + range->min;
	      if (v != array[i])
		{
		  array[i] = v;
		  if (info)
		    *info |= SANE_INFO_INEXACT;
		}
	    }
	}
      break;

    case SANE_CONSTRAINT_WORD_LIST:
      /* If there is no exact match in the list, use the nearest value */
      w = *(SANE_Word *) value;
      word_list = opt->constraint.word_list;
      for (i = 1, k = 1, v = abs (w - word_list[1]); i <= word_list[0]; i++)
	{
	  SANE_Word vh;
	  if ((vh = abs (w - word_list[i])) < v)
	    {
	      v = vh;
	      k = i;
	    }
	}
      if (w != word_list[k])
	{
	  *(SANE_Word *) value = word_list[k];
	  if (info)
	    *info |= SANE_INFO_INEXACT;
	}
      break;

    case SANE_CONSTRAINT_STRING_LIST:
      /* Matching algorithm: take the longest unique match ignoring
         case.  If there is an exact match, it is admissible even if
         the same string is a prefix of a longer option name. */
      string_list = opt->constraint.string_list;
      len = strlen (value);

      /* count how many matches of length LEN characters we have: */
      num_matches = 0;
      match = -1;
      for (i = 0; string_list[i]; ++i)
	if (strncasecmp (value, string_list[i], len) == 0
	    && len <= strlen (string_list[i]))
	  {
	    match = i;
	    if (len == strlen (string_list[i]))
	      {
		/* exact match... */
		if (strcmp (value, string_list[i]) != 0)
		  /* ...but case differs */
		  strcpy (value, string_list[match]);
		return SANE_STATUS_GOOD;
	      }
	    ++num_matches;
	  }

      if (num_matches > 1)
	return SANE_STATUS_INVAL;
      else if (num_matches == 1)
	{
	  strcpy (value, string_list[match]);
	  return SANE_STATUS_GOOD;
	}
      return SANE_STATUS_INVAL;

    case SANE_CONSTRAINT_NONE:
      switch (opt->type)
	{
	case SANE_TYPE_BOOL:
	  b = *(SANE_Bool *) value;
	  if (b != SANE_TRUE && b != SANE_FALSE)
	    return SANE_STATUS_INVAL;
	  break;
	default:
	  break;
	}
    default:
      break;
    }
  return SANE_STATUS_GOOD;
}
