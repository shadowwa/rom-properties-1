/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * ImageDecoder.cpp: Image decoding functions.                             *
 *                                                                         *
 * Copyright (c) 2016 by David Korth.                                      *
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
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.           *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_LIBROMDATA_IMG_IMAGEDECODER_HPP__
#define __ROMPROPERTIES_LIBROMDATA_IMG_IMAGEDECODER_HPP__

#include <stdint.h>

namespace LibRomData {

class rp_image;

class ImageDecoder
{
	private:
		// ImageDecoder is a static class.
		ImageDecoder();
		~ImageDecoder();
		ImageDecoder(const ImageDecoder &other);
		ImageDecoder &operator=(const ImageDecoder &other);

	public:
		/**
		 * Convert a Nintendo DS 16-color + palette image to rp_image.
		 * @param width Image width.
		 * @param height Image height.
		 * @param img_buf CI4 image buffer.
		 * @param img_siz Size of image data. [must be >= (w*h)/2]
		 * @param pal_buf Palette buffer.
		 * @param pal_siz Size of palette data. [must be >= 16*2]
		 * @return rp_image, or nullptr on error.
		 */
		static rp_image *fromNDS_CI4(int width, int height,
			const uint8_t *img_buf, int img_siz,
			const uint16_t *pal_buf, int pal_siz);

		/**
		 * Convert a GameCube RGB5A3 image to rp_image.
		 * @param width Image width.
		 * @param height Image height.
		 * @param img_buf RGB5A3 image buffer.
		 * @param img_siz Size of image data. [must be >= (w*h)*2]
		 * @return rp_image, or nullptr on error.
		 */
		static rp_image *fromGcnRGB5A3(int width, int height,
			const uint16_t *img_buf, int img_siz);

		/**
		 * Convert a GameCube CI8 image to rp_image.
		 * @param width Image width.
		 * @param height Image height.
		 * @param img_buf CI8 image buffer.
		 * @param img_siz Size of image data. [must be >= (w*h)]
		 * @param pal_buf Palette buffer.
		 * @param pal_siz Size of palette data. [must be >= 256*2]
		 * @return rp_image, or nullptr on error.
		 */
		static rp_image *fromGcnCI8(int width, int height,
			const uint8_t *img_buf, int img_siz,
			const uint16_t *pal_buf, int pal_siz);
};

}

#endif /* __ROMPROPERTIES_LIBROMDATA_IMG_IMAGEDECODER_HPP__ */