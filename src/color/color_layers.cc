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

#include "color/color_layers.h"

BEGIN_YAFARAY

ColorLayers::ColorLayers(const Layers &layers)
{
	for(const auto &layer : layers)
	{
		set(layer.first, LayerDef::getDefaultColor(layer.first));
		flags_ |= layer.second.getFlags();
	}
}

void ColorLayers::setDefaultColors()
{
	for(auto &item : items_)
	{
		item.second = LayerDef::getDefaultColor(item.first);
	}
}

bool ColorLayers::isDefinedAny(const std::vector<LayerDef::Type> &types) const
{
	for(const auto &type : types)
	{
		if(find(type)) return true;
	}
	return false;
}


END_YAFARAY