/****************************************************************************
 *      directlight.cc: an integrator for direct lighting only
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

#include "integrator/surface/integrator_debug.h"
#include "geometry/surface.h"
#include "common/param.h"
#include "accelerator/accelerator.h"
#include "render/imagesplitter.h"

BEGIN_YAFARAY

DebugIntegrator::DebugIntegrator(RenderControl &render_control, Logger &logger, SurfaceProperties dt) : TiledIntegrator(render_control, logger), debug_type_(dt)
{
	render_info_ += "Debug integrator: '";
	switch(dt)
	{
		case N:
			render_info_ += "N";
			break;
		case DPdU:
			render_info_ += "dPdU";
			break;
		case DPdV:
			render_info_ += "dPdV";
			break;
		case Nu:
			render_info_ += "NU";
			break;
		case Nv:
			render_info_ += "NV";
			break;
		case DSdU:
			render_info_ += "dSdU";
			break;
		case DSdV:
			render_info_ += "dSdV";
			break;
	}

	render_info_ += "' | ";
}

bool DebugIntegrator::preprocess(const RenderView *render_view, ImageFilm *image_film, const Scene &scene)
{
	bool success = SurfaceIntegrator::preprocess(render_view, image_film, scene);
	return success;
}

std::pair<Rgb, float> DebugIntegrator::integrate(int thread_id, int ray_level, bool chromatic_enabled, float wavelength, Ray &ray, int additional_depth, const RayDivision &ray_division, ColorLayers *color_layers, RandomGenerator &random_generator, const PixelSamplingData &pixel_sampling_data) const
{
	std::unique_ptr<const SurfacePoint> sp;
	float intersect_tmax;
	std::tie(sp, intersect_tmax) = accelerator_->intersect(ray, camera_);
	if(sp)
	{
		Rgb col {0.f};
		if(debug_type_ == N)
			col = Rgb((sp->n_.x_ + 1.f) * .5f, (sp->n_.y_ + 1.f) * .5f, (sp->n_.z_ + 1.f) * .5f);
		else if(debug_type_ == DPdU)
			col = Rgb((sp->dp_du_.x_ + 1.f) * .5f, (sp->dp_du_.y_ + 1.f) * .5f, (sp->dp_du_.z_ + 1.f) * .5f);
		else if(debug_type_ == DPdV)
			col = Rgb((sp->dp_dv_.x_ + 1.f) * .5f, (sp->dp_dv_.y_ + 1.f) * .5f, (sp->dp_dv_.z_ + 1.f) * .5f);
		else if(debug_type_ == Nu)
			col = Rgb((sp->nu_.x_ + 1.f) * .5f, (sp->nu_.y_ + 1.f) * .5f, (sp->nu_.z_ + 1.f) * .5f);
		else if(debug_type_ == Nv)
			col = Rgb((sp->nv_.x_ + 1.f) * .5f, (sp->nv_.y_ + 1.f) * .5f, (sp->nv_.z_ + 1.f) * .5f);
		else if(debug_type_ == DSdU)
			col = Rgb((sp->ds_du_.x_ + 1.f) * .5f, (sp->ds_du_.y_ + 1.f) * .5f, (sp->ds_du_.z_ + 1.f) * .5f);
		else if(debug_type_ == DSdV)
			col = Rgb((sp->ds_dv_.x_ + 1.f) * .5f, (sp->ds_dv_.y_ + 1.f) * .5f, (sp->ds_dv_.z_ + 1.f) * .5f);
		return {col, 1.f};
	}
	return {{0.f}, 1.f};
}

std::unique_ptr<Integrator> DebugIntegrator::factory(Logger &logger, ParamMap &params, const Scene &scene, RenderControl &render_control)
{
	int dt = 1;
	bool pn = false;
	params.getParam("debugType", dt);
	params.getParam("showPN", pn);
	std::cout << "debugType " << dt << std::endl;
	auto inte = std::unique_ptr<DebugIntegrator>(new DebugIntegrator(render_control, logger, static_cast<SurfaceProperties>(dt)));
	inte->show_pn_ = pn;

	return inte;
}

END_YAFARAY
