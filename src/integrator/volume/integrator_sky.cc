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

#include "integrator/volume/integrator_sky.h"
#include "scene/scene.h"
#include "material/material.h"
#include "background/background.h"
#include "light/light.h"
#include "common/param.h"
#include "render/render_data.h"
#include "photon/photon.h"

BEGIN_YAFARAY

SkyIntegrator::SkyIntegrator(Logger &logger, float s_size, float a, float ss, float t) : VolumeIntegrator(logger)
{
	step_size_ = s_size;
	alpha_ = a;
	//sigma_t = ss;
	turbidity_ = t;
	scale_ = ss;

	alpha_r_ = 0.1136f * alpha_; // rayleigh, molecules
	alpha_m_ = 0.8333f * alpha_; // mie, haze

	// beta m for rayleigh scattering

	float N = 2.545e25f;
	float n = 1.0003f;
	float p_n = 0.035f;
	float l = 500e-9f;

	b_r_ = 8 * math::squared_pi * math::num_pi * (n * n - 1) * (n * n - 1) / (3 * N * l * l * l * l) * (6 + 3 * p_n) / (6 - 7 * p_n); // * sigma_t;

	// beta p for mie scattering

	float T = turbidity_;
	float c = (0.6544 * T - 0.651) * 1e-16f;
	float v = 4.f;
	float k = 0.67f;

	b_m_ = 0.434 * c * math::num_pi * powf(2 * math::num_pi / l, v - 2) * k * 0.01; // * sigma_t; // FIXME: bad, 0.01 scaling to make it look better

	std::cout << "SkyIntegrator: b_m: " << b_m_ << " b_r: " << b_r_ << std::endl;
}

bool SkyIntegrator::preprocess(ImageFilm *image_film, const RenderView *render_view, const Scene &scene)
{
	bool success = VolumeIntegrator::preprocess(image_film, render_view, scene);
	background_ = scene.getBackground();
	success = success && static_cast<bool>(background_);
	return success;
}

Rgb SkyIntegrator::skyTau(const Ray &ray) const
{
	//std::cout << " ray.from: " << ray.from << " ray.dir: " << ray.dir << " ray.tmax: " << ray.tmax << " t0: " << t0 << " t1: " << t1 << std::endl;
	/*
	if (ray.tmax < t0 && ! (ray.tmax < 0)) return {0.f};
	if (ray.tmax < t1 && ! (ray.tmax < 0)) t1 = ray.tmax;
	if (t0 < 0.f) t0 = 0.f;
	*/
	float dist;
	if(ray.tmax_ < 0.f) dist = 1000.f;
	else dist = ray.tmax_;
	if(dist < 0.f) dist = 1000.f;
	const float s = dist;
	const float cos_theta = ray.dir_.z(); //vector3d_t(0.f, 0.f, 1.f) * ray.dir;
	const float h_0 = ray.from_.z();
	/*
	float K = - sigma_t / (alpha * cos_theta);
	float H = exp(-alpha * h0);
	float u = exp(-alpha * (h0 + s * cos_theta));
	tauVal = Rgba(K*(H-u));
	*/
	return Rgb{sigma_t_ * math::exp(-alpha_ * h_0) * (1.f - math::exp(-alpha_ * cos_theta * s)) / (alpha_ * cos_theta)};
	//std::cout << tauVal.energy() << " " << cos_theta << " " << dist << " " << ray.tmax << std::endl;
	//return Rgba(exp(-result.getR()), exp(-result.getG()), exp(-result.getB()));
}

Rgb SkyIntegrator::skyTau(const Ray &ray, float beta, float alpha) const
{
	if(ray.tmax_ < 0.f) return Rgb{0.f};
	const float s = ray.tmax_ * scale_;
	float cos_theta = ray.dir_.z();
	float h_0 = ray.from_.z() * scale_;
	return Rgb{beta * math::exp(-alpha * h_0) * (1.f - math::exp(-alpha * cos_theta * s)) / (alpha * cos_theta)};
	//tauVal = Rgba(-beta / (alpha * cos_theta) * ( exp(-alpha * (h0 + cos_theta * s)) - exp(-alpha*h0) ));
}

Rgb SkyIntegrator::transmittance(RandomGenerator &random_generator, const Ray &ray) const
{
	//return Rgba(0.f);
	Rgb result = skyTau(ray, b_m_, alpha_m_);
	result += skyTau(ray, b_r_, alpha_r_);
	return Rgb(math::exp(-result.energy()));
}

Rgb SkyIntegrator::integrate(RandomGenerator &random_generator, const Ray &ray, int additional_depth) const
{
	if(ray.tmax_ < 0.f) return Rgb{0.f};
	const float s = ray.tmax_ * scale_;
	const int v_vec = 3;
	const int u_vec = 8;
	Rgb s_0_m {0.f}, s_0_r {0.f};
	for(int v = 0; v < v_vec; v++)
	{
		const float theta = (v * 0.3f + 0.2f) * 0.5f * math::num_pi;
		for(int u = 0; u < u_vec; u++)
		{
			const float phi = (u /* + (*chromatic_data.random_generator)() */) * 2.0f * math::num_pi / (float)u_vec;
			const float z = math::cos(theta);
			const float x = math::sin(theta) * math::cos(phi);
			const float y = math::sin(theta) * math::sin(phi);
			const Vec3 w{x, y, z};
			const Ray bgray({0, 0, 0}, w, 0, 1, 0);
			const Rgb l_s = background_ ? background_->eval(bgray.dir_) : Rgb(0.f);
			const float b_r_angular = b_r_ * 3 / (2 * math::num_pi * 8) * (1.0f + (w * (-ray.dir_)) * (w * (-ray.dir_)));
			const float k = 0.67f;
			const float angle = math::acos(w * (ray.dir_));
			const float b_m_angular = b_m_ / (2 * k * math::num_pi) * mieScatter(angle);
			//std::cout << "w: " << w << " theta: " << theta << " -ray.dir: " << -ray.dir << " angle: " << angle << " mie ang " << b_m_angular << std::endl;
			s_0_m = s_0_m + l_s * b_m_angular;
			s_0_r = s_0_r + l_s * b_r_angular;
		}
		//std::cout << "w: " << w << " theta: " << theta << " phi: " << phi << " S0: " << S0 << std::endl;
	}

	s_0_r = s_0_r * (1.f / static_cast<float>(u_vec * v_vec)); //* 10.f; // FIXME: bad again, *10 scaling to make it look better
	s_0_m = s_0_m * (1.f / static_cast<float>(u_vec * v_vec));

	//std::cout << " S0_m: " << S0_m.energy() << std::endl;

	const float cos_theta = ray.dir_.z();
	const float h_0 = ray.from_.z() * scale_;
	const float step = step_size_ * scale_;
	float pos = 0.f + random_generator() * step;
	Rgb i_r {0.f}, i_m {0.f};
	while(pos < s)
	{
		const Ray step_ray(ray.from_, ray.dir_, 0, pos / scale_, 0);
		const float u_r = math::exp(-alpha_r_ * (h_0 + pos * cos_theta));
		const float u_m = math::exp(-alpha_m_ * (h_0 + pos * cos_theta));
		const Rgb tau_m = skyTau(step_ray, b_m_, alpha_m_);
		const Rgb tau_r = skyTau(step_ray, b_r_, alpha_r_);
		const float tr_r = math::exp(-tau_r.energy());
		const float tr_m = math::exp(-tau_m.energy());
		//if (Tr < 1e-3) // || u < 1e-3)
		//	break;
		i_r += Rgb{tr_r * u_r * step};
		i_m += Rgb{tr_m * u_m * step};
		//std::cout << /* tau_r.energy() << " " << Tr_r << " " <<  */ I_m.energy() << " " << Tr_m << " " << pos << " " << cos_theta << std::endl;
		pos += step;
	}
	//std::cout << "result: " << S0_m * I_m << " " << I_m.energy() << " " << S0_m.energy() << std::endl;
	return s_0_r * i_r + s_0_m * i_m;
}

float SkyIntegrator::mieScatter(float theta)
{
	theta *= 360 / math::mult_pi_by_2;
	if(theta < 1.f)
		return 4.192;
	if(theta < 4.f)
		return (1.f - ((theta - 1.f) / 3.f)) * 4.192 + ((theta - 1.f) / 3.f) * 3.311;
	if(theta < 7.f)
		return (1.f - ((theta - 4.f) / 3.f)) * 3.311 + ((theta - 4.f) / 3.f) * 2.860;
	if(theta < 10.f)
		return (1.f - ((theta - 7.f) / 3.f)) * 2.860 + ((theta - 7.f) / 3.f) * 2.518;
	if(theta < 30.f)
		return (1.f - ((theta - 10.f) / 20.f)) * 2.518 + ((theta - 10.f) / 20.f) * 1.122;
	if(theta < 60.f)
		return (1.f - ((theta - 30.f) / 30.f)) * 1.122 + ((theta - 30.f) / 30.f) * 0.3324;
	if(theta < 80.f)
		return (1.f - ((theta - 60.f) / 20.f)) * 0.3324 + ((theta - 60.f) / 20.f) * 0.1644;

	return (1.f - ((theta - 80.f) / 100.f)) * 0.1644f + ((theta - 80.f) / 100.f) * 0.1;
}

Integrator * SkyIntegrator::factory(Logger &logger, const ParamMap &params, const Scene &scene, RenderControl &render_control)
{
	float s_size = 1.f;
	float a = .5f;
	float ss = .1f;
	float t = 3.f;
	params.getParam("stepSize", s_size);
	params.getParam("sigma_t", ss);
	params.getParam("alpha", a);
	params.getParam("turbidity", t);
	return new SkyIntegrator(logger, s_size, a, ss, t);
}


END_YAFARAY
