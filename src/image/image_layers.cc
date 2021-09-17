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
 */

#include "image/image_layers.h"
#include "color/color_layers.h"

BEGIN_YAFARAY

int ImageLayer::getWidth() const
{
	if(!image_) return 0;
	else return image_->getWidth();
}

int ImageLayer::getHeight() const
{
	if(!image_) return 0;
	else return image_->getHeight();
}

void ImageLayers::setColor(int x, int y, const ColorLayer &color_layer)
{
	ImageLayer *image_layer = find(color_layer.layer_type_);
	if(image_layer) image_layer->image_->setColor(x, y, color_layer.color_);
}

Rgba ImageLayers::getColor(int x, int y, const Layer &layer) const
{
	const ImageLayer *image_layer = find(layer.getType());
	if(image_layer) return image_layer->image_->getColor(x, y);
	else return {0.f};
}

int ImageLayers::getWidth() const
{
	int width = 0;
	//Obtain the maximum dimension across all images in the image layers
	for(const auto &image_layer : items_)
	{
		const int image_width = image_layer.second.getWidth();
		if(width < image_width) width = image_width;
	}
	return width;
}

int ImageLayers::getHeight() const
{
	int height = 0;
	//Obtain the maximum dimension across all images in the image layers
	for(const auto &image_layer : items_)
	{
		const int image_width = image_layer.second.getWidth();
		if(height < image_width) height = image_width;
	}
	return height;
}

END_YAFARAY
