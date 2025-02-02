#pragma once
/****************************************************************************
 *      meshlight.h: a light source using a triangle mesh as shape
 *      This is part of the libYafaRay package
 *      Copyright (C) 2006  Mathias Wein
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

#ifndef YAFARAY_LIGHT_MESHLIGHT_H
#define YAFARAY_LIGHT_MESHLIGHT_H

#include "light/light.h"
#include "light_ies.h"
#include <vector>
#include <memory>

BEGIN_YAFARAY

class Object;
class Primitive;
class Accelerator;
class Pdf1D;
class ParamMap;
class Scene;

class ObjectLight final : public Light
{
	public:
		static Light *factory(Logger &logger, const Scene &scene, const std::string &name, const ParamMap &params);

	private:
		ObjectLight(Logger &logger, const std::string &object_name, const Rgb &col, int sampl, bool dbl_s = false, bool light_enabled = true, bool cast_shadows = true);
		void init(Scene &scene) override;
		Rgb totalEnergy() const override;
		Rgb emitPhoton(float s_1, float s_2, float s_3, float s_4, Ray &ray, float &ipdf) const override;
		Rgb emitSample(Vec3 &wo, LSample &s) const override;
		bool diracLight() const override { return false; }
		bool illumSample(const SurfacePoint &sp, LSample &s, Ray &wi) const override;
		bool illuminate(const SurfacePoint &sp, Rgb &col, Ray &wi) const override { return false; }
		int nSamples() const override { return samples_; }
		bool canIntersect() const override { return accelerator_ != nullptr; }
		bool intersect(const Ray &ray, float &t, Rgb &col, float &ipdf) const override;
		float illumPdf(const SurfacePoint &sp, const SurfacePoint &sp_light) const override;
		void emitPdf(const SurfacePoint &sp, const Vec3 &wi, float &area_pdf, float &dir_pdf, float &cos_wo) const override;
		void initIs();
		std::pair<Point3, Vec3> sampleSurface(float s_1, float s_2) const;

		std::string object_name_;
		bool double_sided_;
		Rgb color_;
		std::unique_ptr<Pdf1D> area_dist_;
		std::vector<const Primitive *> primitives_;
		int samples_;
		int num_primitives_; //!< gives the array size of uDist
		float area_, inv_area_;
		Object *base_object_ = nullptr;
		std::unique_ptr<const Accelerator> accelerator_;
};

END_YAFARAY

#endif // YAFARAY_LIGHT_MESHLIGHT_H
