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

#ifndef YAFARAY_IMAGE_MANIPULATION_H
#define YAFARAY_IMAGE_MANIPULATION_H

#include "common/yafaray_common.h"
#include "image/image.h"
#include "image/image_buffers.h"

BEGIN_YAFARAY

class Image;
class Logger;
class DenoiseParams;
struct EdgeToonParams;
struct DenoiseParams;
class ImageLayers;

namespace image_manipulation
{
	Image *getDenoisedLdrImage(Logger &logger, const Image *image, const DenoiseParams &denoise_params);
	Image *getComposedImage(Logger &logger, const Image *image_1, const Image *image_2, const Image::Position &position_image_2, int overlay_x = 0, int overlay_y = 0);
	bool drawTextInImage(Logger &logger, Image *image, const std::string &text_utf_8, float font_size_factor, const std::string &font_path);
	void generateDebugFacesEdges(ImageLayers &film_image_layers, int xstart, int width, int ystart, int height, bool drawborder, const EdgeToonParams &edge_params, const ImageBuffer2D<Gray> &weights);
	void generateToonAndDebugObjectEdges(ImageLayers &film_image_layers, int xstart, int width, int ystart, int height, bool drawborder, const EdgeToonParams &edge_params, const ImageBuffer2D<Gray> &weights);
	int generateMipMaps(Logger &logger, std::vector<std::shared_ptr<Image>> &images);
	std::string printDenoiseParams(const DenoiseParams &denoise_params);
	void logWarningsMissingLibraries(Logger &logger);
}

END_YAFARAY

#endif //YAFARAY_IMAGE_MANIPULATION_H
