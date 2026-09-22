/*
 * rsc_load.c Copyright(c) 2023 Thorsten Otto
 *
 * This file is in the public domain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __PUREC__
#include <aes.h>
#include <tos.h>
#else
#include <gem.h>
#include <osbind.h>
#endif
#include "rsc_load.h"

/*
 * configuration option:
 * enable/disable some extra checks.
 * Disabling them will save a few bytes
 * (AES does not do these checks either)
 */
#define WITH_CHECKS 0
#define WITH_COLORICONS 1

/*
 * entries in the array that is only present in resources with color-icons
 */
#define RSC_EXT_FILESIZE	0
#define RSC_EXT_CICONBLK	1
#define RSC_EXT_PALETTE		2
#define RSC_EXT_ENDMARK		3
#define RSC_EXT_SIZE		4	/* number of known extension slots */

#ifndef G_EXTBOX
#define G_EXTBOX          40                              /* XaAES */
#endif

/******************************************************************************/
/*** ---------------------------------------------------------------------- ***/
/******************************************************************************/

static void rsc_obfix(OBJECT *tree, _ULONG count)
{
	_WORD wchar, hchar, dummy;
	
	graf_handle(&wchar, &hchar, &dummy, &dummy);
	while (count)
	{
		tree->ob_x = wchar * (tree->ob_x & 0xff) + (tree->ob_x >> 8);
		tree->ob_y = hchar * (tree->ob_y & 0xff) + (tree->ob_y >> 8);
		tree->ob_width = wchar * (tree->ob_width & 0xff) + (tree->ob_width >> 8);
		tree->ob_height = hchar * (tree->ob_height & 0xff) + (tree->ob_height >> 8);
		tree++;
		count--;
	}
}

/*** ---------------------------------------------------------------------- ***/

static void xrsrc_hdr2xrsc(RSXHDR *xrsc_header, const RSHDR *header)
{
	xrsc_header->rsh_vrsn	 = 3 | (header->rsh_vrsn & ~RSC_VERSION_MASK);
	xrsc_header->rsh_extvrsn = XRSC_VRSN_ORCS; /* informational only; not used in code */
	xrsc_header->rsh_object  = header->rsh_object;
	xrsc_header->rsh_tedinfo = header->rsh_tedinfo;
	xrsc_header->rsh_iconblk = header->rsh_iconblk;
	xrsc_header->rsh_bitblk  = header->rsh_bitblk;
	xrsc_header->rsh_frstr	 = header->rsh_frstr;
	xrsc_header->rsh_string  = header->rsh_string;
	xrsc_header->rsh_imdata  = header->rsh_imdata;
	xrsc_header->rsh_frimg	 = header->rsh_frimg;
	xrsc_header->rsh_trindex = header->rsh_trindex;
	xrsc_header->rsh_nobs	 = header->rsh_nobs;
	xrsc_header->rsh_ntree	 = header->rsh_ntree;
	xrsc_header->rsh_nted	 = header->rsh_nted;
	xrsc_header->rsh_nib	 = header->rsh_nib;
	xrsc_header->rsh_nbb	 = header->rsh_nbb;
	xrsc_header->rsh_nstring = header->rsh_nstring;
	xrsc_header->rsh_nimages = header->rsh_nimages;
	xrsc_header->rsh_rssize  = header->rsh_rssize;
}

/*** ---------------------------------------------------------------------- ***/

#if WITH_CHECKS
static _ULONG filesize;
#endif

#ifdef __arm__
#define RSC_UNUSED __attribute__((unused))
#else
#define RSC_UNUSED
#endif
static int RSC_UNUSED xrsrc_fix(RSHDR *rscHdr)
{
	RSXHDR xrsc_header;
	_ULONG UObj;
	char *buf = (char *)rscHdr;
#if WITH_COLORICONS
	CICONBLK *cicon_p = NULL;
#endif
	
	if (!IS_XRSC_HEADER(rscHdr))
		xrsrc_hdr2xrsc(&xrsc_header, rscHdr);
	else
		xrsc_header = *((RSXHDR *)rscHdr);

#define rsc_pointer(offset) (buf + (size_t)offset)
#define rschdr_pointer(offset) rsc_pointer(xrsc_header.offset)
	
	/* find color icons */
	if (xrsc_header.rsh_vrsn & RSC_EXT_FLAG)
	{
#if WITH_COLORICONS
		_LONG *p;
		_LONG *cp;
#if WITH_CHECKS
		int ok = TRUE;
#endif

		p = (_LONG *)rschdr_pointer(rsh_rssize);
#if WITH_CHECKS
		if ((_ULONG)p[RSC_EXT_FILESIZE] != filesize)
		{
			ok = FALSE;
		} else if (p[RSC_EXT_CICONBLK] < 0 || (_ULONG)p[RSC_EXT_CICONBLK] >= filesize)
		{
			ok = FALSE;
		} else
#endif
		{
			/*
			 * p now points to the extension header.
			 * p[0] is the total file size.
			 * p[1] is the offset to the CICONBLK ptrs list
			 * The list is terminated by a zero,
			 * we check whether a palette entry is present.
			 */
			cp = p + RSC_EXT_CICONBLK + 1;
	
			for (;;)
			{
#if WITH_CHECKS
				if ((_ULONG)((char *)cp - buf) >= filesize)
				{
					ok = FALSE;
					break;
				}
#endif
				if (*cp == 0)
					break;
				if (*cp != -1)
				{
					if (cp == (p + RSC_EXT_PALETTE))
					{
#if 0
						_WORD *palette = (_WORD *) (buf + (size_t) (*cp));
	
						/* save palette here if needed */
#endif
					}
					break;
				}
				cp++;
			}
		}

#if WITH_CHECKS
		if (ok != FALSE)
#endif
		{
			p = (_LONG *)rsc_pointer(p[RSC_EXT_CICONBLK]);
			/*
			 * p now points to the CICONBLKs list
			 * In the file, it is just an empty list, terminated by -1.
			 */
#if WITH_CHECKS
			while (ok != FALSE)
			{
				if ((_ULONG)((char *)p - buf) >= filesize)
					ok = FALSE;
				else if (*p == -1)
					break;
				else if (*p != 0)
					ok = FALSE;
				else
					p++;
			}
			if (ok != FALSE && *p == -1)
#else
			for (;;)
			{
				if (*p == -1)
					break;
				else
					p++;
			}
#endif
			{
				p++;
				/*
				 * CICONBLKs follow here
				 */
				cicon_p = (CICONBLK *)p;
			}
		}
#if WITH_CHECKS
		if (ok == FALSE)
		{
			Mfree(buf);
			return FALSE;
		}
#endif
#else /* !WITH_COLORICONS */
		Mfree(buf);
		return FALSE;
#endif
	}

	/* fix objects */
	{
		OBJECT *_rs_object;

		_rs_object = (OBJECT *)rschdr_pointer(rsh_object);
		rsc_obfix(_rs_object, xrsc_header.rsh_nobs);
		for (UObj = 0; UObj < xrsc_header.rsh_nobs; UObj++)
		{
			switch (_rs_object->ob_type & 0xff)
			{
			case G_BOX:
			case G_IBOX:
			case G_BOXCHAR:
			case G_EXTBOX:
				break;
			case G_STRING:
			case G_TITLE:
			case G_BUTTON:
			case G_TEXT:
			case G_FTEXT:
			case G_BOXTEXT:
			case G_FBOXTEXT:
			case G_IMAGE:
			case G_ICON:
				_rs_object->ob_spec.free_string = (char *)rsc_pointer(_rs_object->ob_spec.index);
				break;
			case G_SHORTCUT:
				/* not changed to G_STRING here;
				   might be displayed in window
				   where we use our own functions
				 */
				_rs_object->ob_spec.free_string = (char *)rsc_pointer(_rs_object->ob_spec.index);
				break;
#if WITH_COLORICONS
			case G_CICON:
				if (cicon_p == NULL)
				{
					/* !!! */
					Mfree(buf);
					return FALSE;
				} else
				{
					CICON *dp;
					CICONBLK *cicon;
					_LONG size;
					char *p;
					_LONG num_cicons;
					_LONG idx;

					cicon = cicon_p;
					cicon_p++;
					_rs_object->ob_spec.ciconblk = cicon;
					size = ((cicon->monoblk.ib_wicon + 15) / 16) * 2l * cicon->monoblk.ib_hicon;
					p = (char *)cicon_p;
					cicon->monoblk.ib_pdata = (_WORD *)p;
					p += (size_t)size;
					cicon->monoblk.ib_pmask = (_WORD *)p;
					p += (size_t)size;
					idx = (_LONG)cicon->monoblk.ib_ptext;
					if (idx <= 0 || (buf + (size_t)idx) == p || idx < (_LONG)xrsc_header.rsh_string || idx >= (_LONG)xrsc_header.rsh_rssize)
						cicon->monoblk.ib_ptext = (char *)p;
					else
						cicon->monoblk.ib_ptext = (char *)rsc_pointer(idx);
					p += CICON_STR_SIZE; /* skip reserved space for icon text */
					dp = (CICON *)p;
					num_cicons = (_LONG)(cicon->mainlist);
#if WITH_CHECKS
					if (p > (buf + (size_t)filesize))
					{
						Mfree(buf);
						return FALSE;
					}
#endif
					if (num_cicons == 0)
					{
						cicon->mainlist = NULL;
					} else
					{
						cicon->mainlist = dp;
						while (num_cicons != 0)
						{
							p += sizeof(CICON);
							dp->col_data = (_WORD *)p;
							p += (size_t)(size * dp->num_planes);
							dp->col_mask = (_WORD *)p;
							p += (size_t)size;
							if (dp->sel_data != NULL)
							{
								dp->sel_data = (_WORD *)p;
								p += (size_t)(size * dp->num_planes);
								dp->sel_mask = (_WORD *)p;
								p += (size_t)size;
							} else
							{
								dp->sel_data = NULL;
								dp->sel_mask = NULL;
							}
							num_cicons--;
							if (num_cicons == 0)
							{
								dp->next_res = NULL;
							} else
							{
								dp->next_res = (CICON *)p;
							}
							dp = (CICON *)p;
						}
					}
					cicon_p = (CICONBLK *)p;
				}
				break;
#endif /* WITH_COLORICONS */
			case G_USERDEF:
				_rs_object->ob_spec.userblk = (USERBLK *)rsc_pointer(_rs_object->ob_spec.index);
				/*
				 * It's up to the application to set the appropiate function.
				 * To be on the safe side, let it point to some function
				 * that draws a box only, or simply does nothing.
				 */
				_rs_object->ob_spec.userblk->ub_code = 0;
				break;
			}
			_rs_object++;
		}
	}

	/* fix tedinfos */
	{
		TEDINFO *_rs_tedinfo;

		_rs_tedinfo = (TEDINFO *)rschdr_pointer(rsh_tedinfo);
		for (UObj = 0; UObj < xrsc_header.rsh_nted; UObj++)
		{
			_rs_tedinfo->te_ptext += (size_t)buf;
			_rs_tedinfo->te_ptmplt += (size_t)buf;
			_rs_tedinfo->te_pvalid += (size_t)buf;
			_rs_tedinfo++;
		}
	}

	/* fix iconblks */
	{
		ICONBLK *_rs_iconblk;

		_rs_iconblk = (ICONBLK *)rschdr_pointer(rsh_iconblk);
		for (UObj = 0; UObj < xrsc_header.rsh_nib; UObj++)
		{
			_rs_iconblk->ib_pmask = (_WORD *)rsc_pointer(_rs_iconblk->ib_pmask);
			_rs_iconblk->ib_pdata = (_WORD *)rsc_pointer(_rs_iconblk->ib_pdata);
			_rs_iconblk->ib_ptext = (char *)rsc_pointer(_rs_iconblk->ib_ptext);
			_rs_iconblk++;
		}
	}

	/* fix bitblks */
	{
		BITBLK *_rs_bitblk;

		_rs_bitblk = (BITBLK *)rschdr_pointer(rsh_bitblk);
		for (UObj = 0; UObj < xrsc_header.rsh_nbb; UObj++)
		{
			_rs_bitblk->bi_pdata = (_WORD *)rsc_pointer(_rs_bitblk->bi_pdata);
			_rs_bitblk++;
		}
	}

	/* fix free strings */
	{
		char **_rs_frstr;

		_rs_frstr = (char **)rschdr_pointer(rsh_frstr);
		for (UObj = 0; UObj < xrsc_header.rsh_nstring; UObj++)
		{
			*_rs_frstr += (size_t)buf;
			_rs_frstr++;
		}
	}

	/* fix free images */
	{
		BITBLK **_rs_frimg;

		_rs_frimg = (BITBLK **)rschdr_pointer(rsh_frimg);
		for (UObj = 0; UObj < xrsc_header.rsh_nimages; UObj++)
		{
			*_rs_frimg = (BITBLK *)rsc_pointer(*_rs_frimg);
			_rs_frimg++;
		}
	}

	/* fix tree indices */
	{
		OBJECT **_rs_trindex;

		_rs_trindex = (OBJECT **)rschdr_pointer(rsh_trindex);
#ifdef __GNUC__
		/* avoid type-punned pointer */
		{
			OBJECT ***rscfile = (OBJECT ***)&aes_global[5];
			*rscfile = _rs_trindex;
		}
		{
			void **rscmem = (void **)&aes_global[7];
			*rscmem = buf;
		}
#else
		_AESrscfile = _rs_trindex;
		_AESrscmem = buf;
#endif

		for (UObj = 0; UObj < xrsc_header.rsh_ntree; UObj++)
		{
			*_rs_trindex = (OBJECT *)rsc_pointer(*_rs_trindex);
			_rs_trindex++;
		}
	}
	
#undef rsc_pointer
#undef rschdr_pointer

	return TRUE;
}

/*** ---------------------------------------------------------------------- ***/

#ifdef __arm__

/* RSC files have a fixed, big-endian on-disk layout, even on ARM targets. */
#define DISK_RSHDR_SIZE 36L
#define DISK_OBJECT_SIZE 24L
#define DISK_TEDINFO_SIZE 28L
#define DISK_ICONBLK_SIZE 34L
#define DISK_BITBLK_SIZE 14L

typedef struct
{
	const unsigned char *data;
	long size;
	RSHDR header;
} DISKRSC;

typedef struct
{
	long object, tedinfo, iconblk, bitblk, trindex, frstr, frimg, raw, total;
} RSCLAYOUT;

static _UWORD disk_uword(const DISKRSC *disk, long offset)
{
	const unsigned char *p = disk->data + offset;

	return ((_UWORD)p[0] << 8) | p[1];
}

static _ULONG disk_ulong(const DISKRSC *disk, long offset)
{
	const unsigned char *p = disk->data + offset;

	return ((_ULONG)p[0] << 24) | ((_ULONG)p[1] << 16) | ((_ULONG)p[2] << 8) | p[3];
}

static int disk_range(const DISKRSC *disk, long offset, long length)
{
	return offset >= 0 && length >= 0 && offset <= disk->size && length <= disk->size - offset;
}

static long align_long(long value)
{
	return (value + 3L) & ~3L;
}

static int layout_add(long *offset, long size)
{
	*offset = align_long(*offset);
	if (size < 0 || *offset > 0x7fffffffL - size)
		return FALSE;
	*offset += size;
	return TRUE;
}

static int disk_string(const DISKRSC *disk, _ULONG offset)
{
	long i;

	if (offset == (_ULONG)-1L)
		return TRUE;
	if (offset >= (_ULONG)disk->size)
		return FALSE;
	for (i = (long)offset; i < disk->size; i++)
		if (disk->data[i] == 0)
			return TRUE;
	return FALSE;
}

static int copy_disk_words(_WORD *destination, const DISKRSC *disk, long offset, long words)
{
	long i;

	if (words < 0 || !disk_range(disk, offset, words * 2L))
		return FALSE;
	for (i = 0; i < words; i++)
		destination[i] = (_WORD)disk_uword(disk, offset + i * 2L);
	return TRUE;
}

static int disk_header(DISKRSC *disk, const void *data, long size)
{
	RSHDR *header = &disk->header;

	if (size < DISK_RSHDR_SIZE)
		return FALSE;
	disk->data = data;
	disk->size = size;
	header->rsh_vrsn = (_WORD)disk_uword(disk, 0);
	header->rsh_object = (_WORD)disk_uword(disk, 2);
	header->rsh_tedinfo = (_WORD)disk_uword(disk, 4);
	header->rsh_iconblk = (_WORD)disk_uword(disk, 6);
	header->rsh_bitblk = (_WORD)disk_uword(disk, 8);
	header->rsh_frstr = (_WORD)disk_uword(disk, 10);
	header->rsh_string = (_WORD)disk_uword(disk, 12);
	header->rsh_imdata = (_WORD)disk_uword(disk, 14);
	header->rsh_frimg = (_WORD)disk_uword(disk, 16);
	header->rsh_trindex = (_WORD)disk_uword(disk, 18);
	header->rsh_nobs = (_WORD)disk_uword(disk, 20);
	header->rsh_ntree = (_WORD)disk_uword(disk, 22);
	header->rsh_nted = (_WORD)disk_uword(disk, 24);
	header->rsh_nib = (_WORD)disk_uword(disk, 26);
	header->rsh_nbb = (_WORD)disk_uword(disk, 28);
	header->rsh_nstring = (_WORD)disk_uword(disk, 30);
	header->rsh_nimages = (_WORD)disk_uword(disk, 32);
	header->rsh_rssize = (_WORD)disk_uword(disk, 34);
	return header->rsh_vrsn == 0 && header->rsh_rssize >= DISK_RSHDR_SIZE
		&& header->rsh_rssize <= size;
}

static void set_rsc_globals(void *memory, OBJECT **trees)
{
#ifdef __GNUC__
	memcpy(&aes_global[5], &trees, sizeof(trees));
	memcpy(&aes_global[7], &memory, sizeof(memory));
#else
	_AESrscfile = trees;
	_AESrscmem = memory;
#endif
}

static _WORD xrsrc_load_portable(const void *data, long size)
{
	DISKRSC disk;
	RSCLAYOUT layout;
	RSHDR *header;
	unsigned char *image;
	OBJECT *objects, **trees;
	TEDINFO *tedinfo;
	ICONBLK *iconblk;
	BITBLK *bitblk;
	char **frstr;
	BITBLK **frimg;
	long offset, i, words;
	_ULONG spec;

	if (!disk_header(&disk, data, size))
		return FALSE;
	header = &disk.header;
	if (header->rsh_nobs < 0 || header->rsh_ntree < 0 || header->rsh_nted < 0
		|| header->rsh_nib < 0 || header->rsh_nbb < 0 || header->rsh_nstring < 0 || header->rsh_nimages < 0
		|| !disk_range(&disk, header->rsh_object, (long)header->rsh_nobs * DISK_OBJECT_SIZE)
		|| !disk_range(&disk, header->rsh_tedinfo, (long)header->rsh_nted * DISK_TEDINFO_SIZE)
		|| !disk_range(&disk, header->rsh_iconblk, (long)header->rsh_nib * DISK_ICONBLK_SIZE)
		|| !disk_range(&disk, header->rsh_bitblk, (long)header->rsh_nbb * DISK_BITBLK_SIZE)
		|| !disk_range(&disk, header->rsh_trindex, (long)header->rsh_ntree * 4L)
		|| !disk_range(&disk, header->rsh_frstr, (long)header->rsh_nstring * 4L)
		|| !disk_range(&disk, header->rsh_frimg, (long)header->rsh_nimages * 4L))
		return FALSE;

	offset = sizeof(RSHDR);
	layout.object = align_long(offset); if (!layout_add(&offset, (long)header->rsh_nobs * sizeof(OBJECT))) return FALSE;
	layout.tedinfo = align_long(offset); if (!layout_add(&offset, (long)header->rsh_nted * sizeof(TEDINFO))) return FALSE;
	layout.iconblk = align_long(offset); if (!layout_add(&offset, (long)header->rsh_nib * sizeof(ICONBLK))) return FALSE;
	layout.bitblk = align_long(offset); if (!layout_add(&offset, (long)header->rsh_nbb * sizeof(BITBLK))) return FALSE;
	layout.trindex = align_long(offset); if (!layout_add(&offset, (long)header->rsh_ntree * sizeof(OBJECT *))) return FALSE;
	layout.frstr = align_long(offset); if (!layout_add(&offset, (long)header->rsh_nstring * sizeof(char *))) return FALSE;
	layout.frimg = align_long(offset); if (!layout_add(&offset, (long)header->rsh_nimages * sizeof(BITBLK *))) return FALSE;
	layout.raw = align_long(offset); if (!layout_add(&offset, disk.size)) return FALSE;
	layout.total = offset;
	if (layout.total > 0xffffL)
		return FALSE;

	image = (unsigned char *)Malloc(layout.total);
	if (image == NULL)
	{
		form_error(-(-39) - 31); /* ENOMEM */
		return FALSE;
	}
	memset(image, 0, layout.total);
	memcpy(image + layout.raw, disk.data, disk.size);
	header = (RSHDR *)image;
	*header = disk.header;
	header->rsh_object = (_WORD)layout.object;
	header->rsh_tedinfo = (_WORD)layout.tedinfo;
	header->rsh_iconblk = (_WORD)layout.iconblk;
	header->rsh_bitblk = (_WORD)layout.bitblk;
	header->rsh_frstr = (_WORD)layout.frstr;
	header->rsh_string = (_WORD)(layout.raw + disk.header.rsh_string);
	header->rsh_imdata = (_WORD)(layout.raw + disk.header.rsh_imdata);
	header->rsh_frimg = (_WORD)layout.frimg;
	header->rsh_trindex = (_WORD)layout.trindex;
	header->rsh_rssize = (_WORD)layout.total;
	objects = (OBJECT *)(image + layout.object);
	tedinfo = (TEDINFO *)(image + layout.tedinfo);
	iconblk = (ICONBLK *)(image + layout.iconblk);
	bitblk = (BITBLK *)(image + layout.bitblk);

	for (i = 0; i < header->rsh_nted; i++)
	{
		offset = disk.header.rsh_tedinfo + i * DISK_TEDINFO_SIZE;
		spec = disk_ulong(&disk, offset); if (!disk_string(&disk, spec)) goto fail;
		tedinfo[i].te_ptext = spec == (_ULONG)-1L ? (char *)-1L : (char *)(image + layout.raw + spec);
		spec = disk_ulong(&disk, offset + 4); if (!disk_string(&disk, spec)) goto fail;
		tedinfo[i].te_ptmplt = spec == (_ULONG)-1L ? (char *)-1L : (char *)(image + layout.raw + spec);
		spec = disk_ulong(&disk, offset + 8); if (!disk_string(&disk, spec)) goto fail;
		tedinfo[i].te_pvalid = spec == (_ULONG)-1L ? (char *)-1L : (char *)(image + layout.raw + spec);
		tedinfo[i].te_font = (_WORD)disk_uword(&disk, offset + 12); tedinfo[i].te_fontid = (_WORD)disk_uword(&disk, offset + 14);
		tedinfo[i].te_just = (_WORD)disk_uword(&disk, offset + 16); tedinfo[i].te_color = (_WORD)disk_uword(&disk, offset + 18);
		tedinfo[i].te_fontsize = (_WORD)disk_uword(&disk, offset + 20); tedinfo[i].te_thickness = (_WORD)disk_uword(&disk, offset + 22);
		tedinfo[i].te_txtlen = (_WORD)disk_uword(&disk, offset + 24); tedinfo[i].te_tmplen = (_WORD)disk_uword(&disk, offset + 26);
	}
	for (i = 0; i < header->rsh_nib; i++)
	{
		offset = disk.header.rsh_iconblk + i * DISK_ICONBLK_SIZE;
		iconblk[i].ib_wicon = (_WORD)disk_uword(&disk, offset + 22); iconblk[i].ib_hicon = (_WORD)disk_uword(&disk, offset + 24);
		words = ((iconblk[i].ib_wicon + 15L) / 16L) * iconblk[i].ib_hicon;
		if (iconblk[i].ib_wicon < 0 || iconblk[i].ib_hicon < 0) goto fail;
		spec = disk_ulong(&disk, offset); if (!copy_disk_words((_WORD *)(image + layout.raw + spec), &disk, spec, words)) goto fail; iconblk[i].ib_pmask = (_WORD *)(image + layout.raw + spec);
		spec = disk_ulong(&disk, offset + 4); if (!copy_disk_words((_WORD *)(image + layout.raw + spec), &disk, spec, words)) goto fail; iconblk[i].ib_pdata = (_WORD *)(image + layout.raw + spec);
		spec = disk_ulong(&disk, offset + 8); if (!disk_string(&disk, spec)) goto fail; iconblk[i].ib_ptext = spec == (_ULONG)-1L ? (char *)-1L : (char *)(image + layout.raw + spec);
		iconblk[i].ib_char = (_WORD)disk_uword(&disk, offset + 12); iconblk[i].ib_xchar = (_WORD)disk_uword(&disk, offset + 14); iconblk[i].ib_ychar = (_WORD)disk_uword(&disk, offset + 16); iconblk[i].ib_xicon = (_WORD)disk_uword(&disk, offset + 18); iconblk[i].ib_yicon = (_WORD)disk_uword(&disk, offset + 20); iconblk[i].ib_xtext = (_WORD)disk_uword(&disk, offset + 26); iconblk[i].ib_ytext = (_WORD)disk_uword(&disk, offset + 28); iconblk[i].ib_wtext = (_WORD)disk_uword(&disk, offset + 30); iconblk[i].ib_htext = (_WORD)disk_uword(&disk, offset + 32);
	}
	for (i = 0; i < header->rsh_nbb; i++)
	{
		offset = disk.header.rsh_bitblk + i * DISK_BITBLK_SIZE;
		bitblk[i].bi_wb = (_WORD)disk_uword(&disk, offset + 4); bitblk[i].bi_hl = (_WORD)disk_uword(&disk, offset + 6);
		words = ((long)bitblk[i].bi_wb * bitblk[i].bi_hl) / 2L;
		spec = disk_ulong(&disk, offset); if (!copy_disk_words((_WORD *)(image + layout.raw + spec), &disk, spec, words)) goto fail; bitblk[i].bi_pdata = (_WORD *)(image + layout.raw + spec);
		bitblk[i].bi_x = (_WORD)disk_uword(&disk, offset + 8); bitblk[i].bi_y = (_WORD)disk_uword(&disk, offset + 10); bitblk[i].bi_color = (_WORD)disk_uword(&disk, offset + 12);
	}
	for (i = 0; i < header->rsh_nobs; i++)
	{
		OBJECT *object = &objects[i];
		offset = disk.header.rsh_object + i * DISK_OBJECT_SIZE;
		object->ob_next = (_WORD)disk_uword(&disk, offset); object->ob_head = (_WORD)disk_uword(&disk, offset + 2); object->ob_tail = (_WORD)disk_uword(&disk, offset + 4); object->ob_type = (_UWORD)disk_uword(&disk, offset + 6); object->ob_flags = (_UWORD)disk_uword(&disk, offset + 8); object->ob_state = (_UWORD)disk_uword(&disk, offset + 10);
		spec = disk_ulong(&disk, offset + 12);
		switch (object->ob_type & 0xff) { case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT: if (spec < (_ULONG)disk.header.rsh_tedinfo || (spec - disk.header.rsh_tedinfo) % DISK_TEDINFO_SIZE) goto fail; object->ob_spec.tedinfo = &tedinfo[(spec - disk.header.rsh_tedinfo) / DISK_TEDINFO_SIZE]; break; case G_IMAGE: if (spec < (_ULONG)disk.header.rsh_bitblk || (spec - disk.header.rsh_bitblk) % DISK_BITBLK_SIZE) goto fail; object->ob_spec.bitblk = &bitblk[(spec - disk.header.rsh_bitblk) / DISK_BITBLK_SIZE]; break; case G_ICON: if (spec < (_ULONG)disk.header.rsh_iconblk || (spec - disk.header.rsh_iconblk) % DISK_ICONBLK_SIZE) goto fail; object->ob_spec.iconblk = &iconblk[(spec - disk.header.rsh_iconblk) / DISK_ICONBLK_SIZE]; break; case G_STRING: case G_BUTTON: case G_TITLE: if (!disk_string(&disk, spec)) goto fail; object->ob_spec.free_string = spec == (_ULONG)-1L ? (char *)-1L : (char *)(image + layout.raw + spec); break; default: object->ob_spec.index = (_LONG)spec; break; }
		object->ob_x = (_WORD)disk_uword(&disk, offset + 16); object->ob_y = (_WORD)disk_uword(&disk, offset + 18); object->ob_width = (_WORD)disk_uword(&disk, offset + 20); object->ob_height = (_WORD)disk_uword(&disk, offset + 22);
	}
	trees = (OBJECT **)(image + layout.trindex);
	for (i = 0; i < header->rsh_ntree; i++) { spec = disk_ulong(&disk, disk.header.rsh_trindex + i * 4L); if (spec < (_ULONG)disk.header.rsh_object || (spec - disk.header.rsh_object) % DISK_OBJECT_SIZE || spec >= (_ULONG)(disk.header.rsh_object + (long)header->rsh_nobs * DISK_OBJECT_SIZE)) goto fail; trees[i] = &objects[(spec - disk.header.rsh_object) / DISK_OBJECT_SIZE]; }
	frstr = (char **)(image + layout.frstr);
	for (i = 0; i < header->rsh_nstring; i++) { spec = disk_ulong(&disk, disk.header.rsh_frstr + i * 4L); if (!disk_string(&disk, spec)) goto fail; frstr[i] = spec == (_ULONG)-1L ? (char *)-1L : (char *)(image + layout.raw + spec); }
	frimg = (BITBLK **)(image + layout.frimg);
	for (i = 0; i < header->rsh_nimages; i++) { spec = disk_ulong(&disk, disk.header.rsh_frimg + i * 4L); if (spec < (_ULONG)disk.header.rsh_bitblk || (spec - disk.header.rsh_bitblk) % DISK_BITBLK_SIZE) goto fail; frimg[i] = &bitblk[(spec - disk.header.rsh_bitblk) / DISK_BITBLK_SIZE]; }
	rsc_obfix(objects, header->rsh_nobs);
	set_rsc_globals(image, trees);
	return TRUE;
fail:
	Mfree(image);
	return FALSE;
}

#endif /* __arm__ */

_WORD xrsrc_load(const char *fname)
{
	int fd;
	long size;
	char pathname[128];
	void *useRsc;

	strcpy(pathname, fname);
	if (!shel_find(pathname) || (fd = (int)Fopen(pathname, FO_READ)) < 0)
		return FALSE;
	size = Fseek(0L, fd, SEEK_END);
	Fseek(0L, fd, SEEK_SET);
	useRsc = (RSHDR *)Malloc(size);
	if (useRsc == NULL)
	{
		form_error(-(-39) - 31); /* ENOMEM */
		Fclose(fd);
		return FALSE;
	}
	if (Fread(fd, size, useRsc) != size)
	{
		Mfree(useRsc);
		Fclose(fd);
		return FALSE;
	}
	Fclose(fd);

#if WITH_CHECKS
	filesize = size;
#endif

#ifdef __arm__
	{
		_WORD result = xrsrc_load_portable(useRsc, size);
		Mfree(useRsc);
		return result;
	}
#else
	return xrsrc_fix(useRsc);
#endif
}

/*** ---------------------------------------------------------------------- ***/

_WORD xrsrc_free(void)
{
#ifdef __GNUC__
	/* avoid type-punned pointer */
	{
		void **rscmem = (void **)&aes_global[7];
		Mfree(*rscmem);
	}
#else
	Mfree(_AESrscmem);
#endif
	return TRUE;
}

/*** ---------------------------------------------------------------------- ***/

_WORD xrsrc_gaddr(_WORD type, _WORD idx, void *gaddr)
{
	RSHDR *useRsc;
	RSXHDR xrsc_header;

#ifdef __GNUC__
	/* avoid type-punned pointer */
	{
		void **rscmem = (void **)&aes_global[7];
		useRsc = *rscmem;
	}
#else
	useRsc = _AESrscmem;
#endif
#if WITH_CHECKS
	if (gaddr == NULL || useRsc == NULL)
		return FALSE;
#endif
	*(void **)gaddr = NULL;
	if (IS_XRSC_HEADER(useRsc))
	{
		xrsc_header = *((RSXHDR *)useRsc);
	} else
	{
		xrsrc_hdr2xrsc(&xrsc_header, useRsc);
	}

#define rsc_pointer(offset) ((char *)useRsc + (size_t)offset)
#define rschdr_pointer(offset) rsc_pointer(xrsc_header.offset)

#if WITH_CHECKS
#define CHECK_IDX(field) \
		if (idx < 0 || (_ULONG)idx >= xrsc_header.field) \
			return FALSE;
#else
#define CHECK_IDX(field)
#endif

	switch (type)
	{
	case R_TREE:
		CHECK_IDX(rsh_ntree)
		{
			OBJECT **_rs_trindex = (OBJECT **)rschdr_pointer(rsh_trindex);
			*((OBJECT **)gaddr) = _rs_trindex[idx];
		}
		break;
	case R_OBJECT:
		CHECK_IDX(rsh_nobs)
		{
			OBJECT *_rs_object = (OBJECT *)rschdr_pointer(rsh_object);
			*((OBJECT **)gaddr) = &_rs_object[idx];
		}
		break;
	case R_TEDINFO:
		CHECK_IDX(rsh_nted)
		{
			TEDINFO *_rs_tedinfo = (TEDINFO *)rschdr_pointer(rsh_tedinfo);
			*((TEDINFO **)gaddr) = &_rs_tedinfo[idx];
		}
		break;
	case R_ICONBLK:
		CHECK_IDX(rsh_nib)
		{
			ICONBLK *_rs_iconblk = (ICONBLK *)rschdr_pointer(rsh_iconblk);
			*((ICONBLK **)gaddr) = &_rs_iconblk[idx];
		}
		break;
	case R_BITBLK:
		CHECK_IDX(rsh_nbb)
		{
			BITBLK *_rs_bitblk = (BITBLK *)rschdr_pointer(rsh_bitblk);
			*((BITBLK **)gaddr) = &_rs_bitblk[idx];
		}
		break;
	case R_STRING:
		CHECK_IDX(rsh_nstring)
		{
			char **_rs_frstr = (char **)rschdr_pointer(rsh_frstr);
			*((char **)gaddr) = _rs_frstr[idx];
		}
		break;
	case R_IMAGEDATA:
		CHECK_IDX(rsh_nimages)
		{
			BITBLK **_rs_frimg = (BITBLK **)rschdr_pointer(rsh_frimg);
			*((BITBLK **)gaddr) = _rs_frimg[idx];
		}
		break;
	case R_OBSPEC:
		CHECK_IDX(rsh_nobs)
		{
			OBJECT *_rs_object = (OBJECT *)rschdr_pointer(rsh_object);
			*((_LONG **)gaddr) = &_rs_object[idx].ob_spec.index;
		}
		break;
	case R_TEPTEXT:
		CHECK_IDX(rsh_nted)
		{
			TEDINFO *_rs_tedinfo = (TEDINFO *)rschdr_pointer(rsh_tedinfo);
			*((char ***)gaddr) = &_rs_tedinfo[idx].te_ptext;
		}
		break;
	case R_TEPTMPLT:
		CHECK_IDX(rsh_nted)
		{
			TEDINFO *_rs_tedinfo = (TEDINFO *)rschdr_pointer(rsh_tedinfo);
			*((char ***)gaddr) = &_rs_tedinfo[idx].te_ptmplt;
		}
		break;
	case R_TEPVALID:
		CHECK_IDX(rsh_nted)
		{
			TEDINFO *_rs_tedinfo = (TEDINFO *)rschdr_pointer(rsh_tedinfo);
			*((char ***)gaddr) = &_rs_tedinfo[idx].te_pvalid;
		}
		break;
	case R_IBPMASK:
		CHECK_IDX(rsh_nib)
		{
			ICONBLK *_rs_iconblk = (ICONBLK *)rschdr_pointer(rsh_iconblk);
			*((char ***)gaddr) = (char **)&_rs_iconblk[idx].ib_pmask;
		}
		break;
	case R_IBPDATA:
		CHECK_IDX(rsh_nib)
		{
			ICONBLK *_rs_iconblk = (ICONBLK *)rschdr_pointer(rsh_iconblk);
			*((char ***)gaddr) = (char **)&_rs_iconblk[idx].ib_pdata;
		}
		break;
	case R_IBPTEXT:
		CHECK_IDX(rsh_nib)
		{
			ICONBLK *_rs_iconblk = (ICONBLK *)rschdr_pointer(rsh_iconblk);
			*((char ***)gaddr) = &_rs_iconblk[idx].ib_ptext;
		}
		break;
	case R_BIPDATA:
		CHECK_IDX(rsh_nbb)
		{
			BITBLK *_rs_bitblk = (BITBLK *)rschdr_pointer(rsh_bitblk);
			*((char ***)gaddr) = (char **)&_rs_bitblk[idx].bi_pdata;
		}
		break;
	case R_FRSTR:
		CHECK_IDX(rsh_nstring)
		{
			char **_rs_frstr = (char **)rschdr_pointer(rsh_frstr);
			*((char ***)gaddr) = &_rs_frstr[idx];
		}
		break;
	case R_FRIMG:
		CHECK_IDX(rsh_nimages)
		{
			BITBLK **_rs_frimg = (BITBLK **)rschdr_pointer(rsh_frimg);
			*((BITBLK ***)gaddr) = &_rs_frimg[idx];
		}
		break;
	default:
		return FALSE;
	}

#undef rsc_pointer
#undef rschdr_pointer
#undef CHECK_IDX

	return TRUE;
}
