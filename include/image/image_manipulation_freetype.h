#pragma once
/****************************************************************************
 *      This is part of the libYafaRay package
 *
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public
 *      License as published by the Free Software Foundation; either
 *      version 2.1 of the License, or (at your option) any later version.
 *
 *      This library is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef YAFARAY_IMAGE_MANIPULATION_FREETYPE_H
#define YAFARAY_IMAGE_MANIPULATION_FREETYPE_H

#include "resource/guifont.h"

struct FT_Bitmap_;

BEGIN_YAFARAY

namespace image_manipulation_freetype
{
	void drawFontBitmap(FT_Bitmap_ *bitmap, Image *badge_image, int x, int y);
	bool drawTextInImage(Logger &logger, Image *image, const std::string &text_utf_8, float font_size_factor, const std::string &font_path);
}

END_YAFARAY

#endif //YAFARAY_IMAGE_MANIPULATION_FREETYPE_H
