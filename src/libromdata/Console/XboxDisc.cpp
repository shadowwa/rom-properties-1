/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * XboxDisc.cpp: Microsoft Xbox disc image parser.                         *
 *                                                                         *
 * Copyright (c) 2019 by David Korth.                                      *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published by the   *
 * Free Software Foundation; either version 2 of the License, or (at your  *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ***************************************************************************/

#include "XboxDisc.hpp"
#include "librpbase/RomData_p.hpp"

#include "../iso_structs.h"
#include "xdvdfs_structs.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
#include "libi18n/i18n.h"
using namespace LibRpBase;

// Other RomData subclasses
#include "Other/ISO.hpp"

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>
#include <cstring>
#include <ctime>

// C++ includes.
#include <string>
#include <vector>
using std::string;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(XboxDisc)

class XboxDiscPrivate : public LibRpBase::RomDataPrivate
{
	public:
		XboxDiscPrivate(XboxDisc *q, LibRpBase::IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(XboxDiscPrivate)

	public:
		// Disc type.
		enum DiscType {
			DISC_UNKNOWN = -1,	// Unknown disc type.

			DISC_TYPE_EXTRACTED	= 0,	// Extracted XDVDFS
			DISC_TYPE_XGD1		= 1,	// XGD1 (Original Xbox)
			DISC_TYPE_XGD2		= 2,	// XGD2 (Xbox 360)
			DISC_TYPE_XGD3		= 3,	// XGD3 (Xbox 360)
		};
		int discType;
		uint8_t wave;

		// XDVDFS starting address.
		int64_t xdvdfs_addr;

		// XDVDFS header.
		// All fields are byteswapped in the constructor.
		XDVDFS_Header xdvdfsHeader;
};

/** XboxDiscPrivate **/

XboxDiscPrivate::XboxDiscPrivate(XboxDisc *q, IRpFile *file)
	: super(q, file)
	, discType(DISC_UNKNOWN)
	, wave(0)
	, xdvdfs_addr(0)
{
	// Clear the various headers.
	memset(&xdvdfsHeader, 0, sizeof(xdvdfsHeader));
}

/** XboxDisc **/

/**
 * Read a Microsoft Xbox disc image.
 *
 * A ROM file must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM image.
 */
XboxDisc::XboxDisc(IRpFile *file)
	: super(new XboxDiscPrivate(this, file))
{
	// This class handles disc images.
	RP_D(XboxDisc);
	d->className = "XboxDisc";
	d->fileType = FTYPE_DISC_IMAGE;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// TODO: Also check for trimmed XDVDFS. (offset == 0)

	// Read the ISO-9660 PVD.
	// NOTE: Only 2048-byte sectors, since this is DVD.
	ISO_Primary_Volume_Descriptor pvd;
	size_t size = d->file->seekAndRead(ISO_PVD_ADDRESS_2048, &pvd, sizeof(pvd));
	if (size != sizeof(pvd)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this disc image is supported.
	d->discType = isRomSupported_static(&pvd, &d->wave);

	switch (d->discType) {
		case XboxDiscPrivate::DISC_TYPE_XGD1:
			d->xdvdfs_addr = XDVDFS_LBA_OFFSET_XGD1 * XDVDFS_BLOCK_SIZE;
			break;
		case XboxDiscPrivate::DISC_TYPE_XGD2:
			d->xdvdfs_addr = XDVDFS_LBA_OFFSET_XGD2 * XDVDFS_BLOCK_SIZE;
			break;
		case XboxDiscPrivate::DISC_TYPE_XGD3:
			d->xdvdfs_addr = XDVDFS_LBA_OFFSET_XGD3 * XDVDFS_BLOCK_SIZE;
			break;
		default:
			// This might be an extracted XDVDFS.
			d->xdvdfs_addr = 0;
			break;
	}

	// Read the XDVDFS header.
	size = d->file->seekAndRead(
		d->xdvdfs_addr + (XDVDFS_HEADER_LBA_OFFSET * XDVDFS_BLOCK_SIZE),
		&d->xdvdfsHeader, sizeof(d->xdvdfsHeader));
	if (size != sizeof(d->xdvdfsHeader)) {
		// Seek and/or read error.
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Verify the magic strings.
	if (memcmp(d->xdvdfsHeader.magic, XDVDFS_MAGIC, sizeof(d->xdvdfsHeader.magic)) != 0 ||
	    memcmp(d->xdvdfsHeader.magic_footer, XDVDFS_MAGIC, sizeof(d->xdvdfsHeader.magic_footer)) != 0)
	{
		// One or both of the magic strings are incorrect.
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Magic strings are correct.
	if (d->discType <= XboxDiscPrivate::DISC_UNKNOWN) {
		// This is an extracted XDVDFS.
		d->discType = XboxDiscPrivate::DISC_TYPE_EXTRACTED;
	}

#if SYS_BYTEORDER == SYS_BIG_ENDIAN
	// Byteswap the fields.
	d->xdvdfsHeader.root_dir_sector	= le32_to_cpu(d->xdvdfsHeader.root_dir_sector);
	d->xdvdfsHeader.root_dir_size	= le32_to_cpu(d->xdvdfsHeader.root_dir_size);
	d->xdvdfsHeader.timestamp	= le64_to_cpu(d->xdvdfsHeader.timestamp);
#endif /* SYS_BYTEORDER == SYS_BIG_ENDIAN */

	// Disc image is ready.
	d->isValid = true;
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int XboxDisc::isRomSupported_static(const DetectInfo *info)
{
	// NOTE: This version is NOT supported for XboxDisc.
	// Use the ISO-9660 PVD check instead.
	RP_UNUSED(info);
	assert(!"Use the ISO-9660 PVD check instead.");
	return -1;
}

/**
 * Is a ROM image supported by this class?
 * @param pvd ISO-9660 Primary Volume Descriptor.
 * @param pWave If non-zero, receives the wave number. (0 if none; non-zero otherwise.)
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int XboxDisc::isRomSupported_static(
	const ISO_Primary_Volume_Descriptor *pvd, uint8_t *pWave)
{
	// Xbox PVDs from the same manufacturing wave
	// match, so we can check the PVD timestamp
	// to determine if this is an Xbox disc.
	// TODO: Other ISO fields?
	assert(pvd != nullptr);
	if (!pvd) {
		// Bad.
		return -1;
	}

	// Get the creation time.
	const time_t btime = RomDataPrivate::pvd_time_to_unix_time(
		pvd->btime.full, pvd->btime.tz_offset);
	if (btime == -1) {
		// Invalid creation time.
		return -1;
	}

	// Compare to known times.
	// TODO: Enum with XGD waves?

	struct xgd_pvd_t {
		uint8_t xgd;	// XGD type. (1, 2, 3)
		uint8_t wave;	// Manufacturing wave.

		// NOTE: Using int32_t as an optimization, since
		// there won't be any Xbox 360 games released
		// after January 2038. (probably...)
		int32_t btime;	// Creation time.
	};

	static const xgd_pvd_t xgd_tbl[] = {
		// XGD1
		{1,  0, 1000334575},	// XGD1: 2001-09-13 10:42:55.00 '0' (+12:00)

		// XGD2
		{2,  1, 1128716326},	// XGD2 Wave 1:  2005-10-07 12:18:46.00 -08:00
		{2,  2, 1141708147},	// XGD2 Wave 2:  2006-03-06 21:09:07.00 -08:00
		{2,  3, 1231977600},	// XGD2 Wave 3:  2009-01-14 16:00:00.00 -08:00
		{2,  4, 1251158400},	// XGD2 Wave 4:  2009-08-24 17:00:00.00 -07:00
		{2,  5, 1254787200},	// XGD2 Wave 5:  2009-10-05 17:00:00.00 -07:00
		{2,  6, 1256860800},	// XGD2 Wave 6:  2009-10-29 17:00:00.00 -07:00
		{2,  7, 1266796800},	// XGD2 Wave 7:  2010-02-21 16:00:00.00 -08:00
		{2,  8, 1283644800},	// XGD2 Wave 8:  2010-09-04 17:00:00.00 -07:00
		{2,  9, 1284595200},	// XGD2 Wave 9:  2010-09-15 17:00:00.00 -07:00
		{2, 10, 1288310400},	// XGD2 Wave 10: 2010-10-28 17:00:00.00 -07:00
		{2, 11, 1295395200},	// XGD2 Wave 11: 2011-01-18 16:00:00.00 -08:00
		{2, 12, 1307923200},	// XGD2 Wave 12: 2011-06-12 17:00:00.00 -07:00
		{2, 13, 1310515200},	// XGD2 Wave 13: 2011-07-12 17:00:00.00 -07:00
		{2, 14, 1323302400},	// XGD2 Wave 14: 2011-12-07 16:00:00.00 -08:00
		{2, 15, 1329868800},	// XGD2 Wave 15: 2012-02-21 16:00:00.00 -08:00
		{2, 16, 1340323200},	// XGD2 Wave 16: 2012-06-21 17:00:00.00 -07:00
		{2, 17, 1352332800},	// XGD2 Wave 17: 2012-11-07 16:00:00.00 -08:00
		{2, 18, 1353283200},	// XGD2 Wave 18: 2012-11-18 16:00:00.00 -08:00
		{2, 19, 1377561600},	// XGD2 Wave 19: 2013-08-26 17:00:00.00 -07:00
		{2, 20, 1430092800},	// XGD2 Wave 20: 2015-04-26 17:00:00.00 -07:00

		// XGD3 does not have shared PVDs per wave,
		// but the timestamps all have a similar pattern:
		// - Year: 2011+
		// - Min, Sec, Csec: 00
		// - Hour and TZ:
		//   - 17, -07:00
		//   - 16, -08:00

		{0, 0, 0}
	};

	// TODO: Use a binary search instead of linear?
	for (const xgd_pvd_t *pXgd = xgd_tbl; pXgd->xgd != 0; pXgd++) {
		if (pXgd->btime == btime) {
			// Found a match!
			if (pWave) {
				*pWave = pXgd->wave;
			}
			return pXgd->xgd;
		}
	}

	// No match in the XGD table.
	// Check for XGD3.
	static const char xgd3_pvd_times[][9+1] = {
		"17000000\xE4",	// 17:00:00.00 -07:00
		"16000000\xE0",	// 16:00:00.00 -08:00

		"\0"
	};
	// TODO: Verify that this works correctly.
	for (const char *pXgd = &xgd3_pvd_times[0][0]; pXgd[0] != '\0'; pXgd++) {
		if (!memcmp(&pvd->btime.full[8], pXgd, 9)) {
			// Found a match!
			if (pWave) {
				*pWave = 0;
			}
			return XboxDiscPrivate::DISC_TYPE_XGD3;
		}
	}

	// Not XGD.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *XboxDisc::systemName(unsigned int type) const
{
	RP_D(const XboxDisc);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// XboxDisc has the same name worldwide, so we can
	// ignore the region selection.
	// TODO: Identify the OS, or list that in the fields instead?
	static_assert(SYSNAME_TYPE_MASK == 3,
		"XboxDisc::systemName() array index optimization needs to be updated.");

	// TODO: Check for default.xbe and/or default.xex
	// to determine if it's Xbox or Xbox 360.
	// For now, assuming >=XGD2 is 360.
	if (d->discType >= XboxDiscPrivate::DISC_TYPE_XGD2) {
		static const char *const sysNames_X360[4] = {
			"Microsoft Xbox 360", "Xbox 360", "X360", nullptr
		};
		return sysNames_X360[type & SYSNAME_TYPE_MASK];
	} else {
		static const char *const sysNames_Xbox[4] = {
			"Microsoft Xbox", "Xbox", "Xbox", nullptr
		};
		return sysNames_Xbox[type & SYSNAME_TYPE_MASK];
	}

	// Should not get here...
	assert(!"XboxDisc::systemName(): Invalid system name.");
	return nullptr;
}

/**
 * Get a list of all supported file extensions.
 * This is to be used for file type registration;
 * subclasses don't explicitly check the extension.
 *
 * NOTE: The extensions do not include the leading dot,
 * e.g. "bin" instead of ".bin".
 *
 * NOTE 2: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *XboxDisc::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".iso",		// ISO
		".xiso",	// Xbox ISO image
		// TODO: More?

		nullptr
	};
	return exts;
}

/**
 * Get a list of all supported MIME types.
 * This is to be used for metadata extractors that
 * must indicate which MIME types they support.
 *
 * NOTE: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *XboxDisc::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types from FreeDesktop.org..
		"application/x-iso9660-image",

		// TODO: XDVDFS?
		nullptr
	};
	return mimeTypes;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int XboxDisc::loadFieldData(void)
{
	RP_D(XboxDisc);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->discType < 0) {
		// Unknown disc type.
		return -EIO;
	}

	// XDVDFS header.
	const XDVDFS_Header *const xdvdfsHeader = &d->xdvdfsHeader;
	d->fields->reserve(2);	// Maximum of 2 fields.
	if (d->discType >= XboxDiscPrivate::DISC_TYPE_XGD2) {
		d->fields->setTabName(0, "Xbox 360");
	} else {
		d->fields->setTabName(0, "Xbox");
	}

	// Disc type
	const char *const s_disc_type = C_("XboxDisc", "Disc Type");
	// NOTE: Not translating "Xbox Game Disc".
	switch (d->discType) {
		case XboxDiscPrivate::DISC_TYPE_EXTRACTED:
			d->fields->addField_string(s_disc_type,
				C_("XboxDisc", "Extracted XDVDFS"));
			break;
		case XboxDiscPrivate::DISC_TYPE_XGD1:
			d->fields->addField_string(s_disc_type, "Xbox Game Disc 1");
			break;
		case XboxDiscPrivate::DISC_TYPE_XGD2:
			d->fields->addField_string(s_disc_type,
				rp_sprintf("Xbox Game Disc 2 (Wave %u)", d->wave));
			break;
		case XboxDiscPrivate::DISC_TYPE_XGD3:
			d->fields->addField_string(s_disc_type, "Xbox Game Disc 3");
			break;
		default:
			d->fields->addField_string(s_disc_type,
				rp_sprintf(C_("RomData", "Unknown (%u)"), d->wave));
			break;
	}

	// Timestamp
	// NOTE: Timestamp is stored in Windows FILETIME format,
	// which is 100ns units since 1601/01/01 00:00:00 UTC.
	// Based on libwin32common/w32time.h.
#define FILETIME_1970 116444736000000000LL	// Seconds between 1/1/1601 and 1/1/1970.
#define HECTONANOSEC_PER_SEC 10000000LL
	time_t timestamp = static_cast<time_t>(
		(xdvdfsHeader->timestamp - FILETIME_1970) / HECTONANOSEC_PER_SEC);
	d->fields->addField_dateTime(C_("XboxDisc", "Timestamp"), timestamp,
		RomFields::RFT_DATETIME_HAS_DATE |
		RomFields::RFT_DATETIME_HAS_TIME);

	// TODO: Get the XBE and/or XEX.

	// ISO object for ISO-9660 PVD
	if (d->discType >= XboxDiscPrivate::DISC_TYPE_XGD1) {
		ISO *const isoData = new ISO(d->file);
		if (isoData->isOpen()) {
			// Add the fields.
			d->fields->addFields_romFields(isoData->fields(),
				RomFields::TabOffset_AddTabs);
		}
		isoData->unref();
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

}
