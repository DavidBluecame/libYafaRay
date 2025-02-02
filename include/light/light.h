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
 */

#ifndef YAFARAY_LIGHT_H
#define YAFARAY_LIGHT_H

#include "common/yafaray_common.h"
#include "common/flags.h"
#include "color/color.h"
#include <sstream>
#include <common/logger.h>
#include <memory>

BEGIN_YAFARAY

class ParamMap;
class Scene;
class SurfacePoint;
class Background;
class Ray;
class Scene;
class Vec3;
class Point3;
struct LSample;

class Light
{
	public:
		static Light *factory(Logger &logger, const Scene &scene, const std::string &name, const ParamMap &params);
		struct Flags : public yafaray::Flags
		{
			Flags() = default;
			Flags(unsigned int flags) : yafaray::Flags(flags) { } // NOLINT(google-explicit-constructor)
			enum Enum : unsigned int { None = 0, DiracDir = 1, Singular = 1 << 1 };
		};
		explicit Light(Logger &logger) : logger_(logger) { }
		Light(Logger &logger, const Flags &flags): flags_(flags), logger_(logger) { }
		virtual ~Light() = default;
		//! allow for preprocessing when scene loading has finished
		virtual void init(Scene &scene) {}
		//! total energy emmitted during whole frame
		virtual Rgb totalEnergy() const = 0;
		//! emit a photon
		virtual Rgb emitPhoton(float s_1, float s_2, float s_3, float s_4, Ray &ray, float &ipdf) const = 0;
		//! create a sample of light emission, similar to emitPhoton, just more suited for bidirectional methods
		/*! fill in s.dirPdf, s.areaPdf, s.col and s.flags, and s.sp if not nullptr */
		virtual Rgb emitSample(Vec3 &wo, LSample &s) const {return Rgb{0.f};};
		//! indicate whether the light has a dirac delta distribution or not
		virtual bool diracLight() const = 0;
		//! illuminate a given surface point, generating sample s, fill in s.sp if not nullptr; Set ray to test visibility by integrator
		/*! fill in s.pdf, s.col and s.flags */
		virtual bool illumSample(const SurfacePoint &sp, LSample &s, Ray &wi) const = 0;
		//! illuminate a given surfance point; Set ray to test visibility by integrator. Only for dirac lights.
		/*!	return false only if no light is emitted towards sp, e.g. outside cone angle of spot light	*/
		virtual bool illuminate(const SurfacePoint &sp, Rgb &col, Ray &wi) const = 0;
		//! indicate whether the light can intersect with a ray (by the sphereIntersect function)
		virtual bool canIntersect() const { return false; }
		//! sphereIntersect the light source with a ray, giving back distance, energy and 1/PDF
		virtual bool intersect(const Ray &ray, float &t, Rgb &col, float &ipdf) const { return false; }
		//! get the pdf for sampling the incoming direction wi at surface point sp (illumSample!)
		/*! this method requires an intersection point with the light (sp_light). Otherwise, use sphereIntersect() */
		virtual float illumPdf(const SurfacePoint &sp, const SurfacePoint &sp_light) const { return 0.f; }
		//! get the pdf values for sampling point sp on the light and outgoing direction wo when emitting energy (emitSample, NOT illumSample)
		/*! sp should've been generated from illumSample or emitSample, and may only be complete enough to call light functions! */
		virtual void emitPdf(const SurfacePoint &sp, const Vec3 &wo, float &area_pdf, float &dir_pdf, float &cos_wo) const { area_pdf = 0.f; dir_pdf = 0.f; }
		//! (preferred) number of samples for direct lighting
		virtual int nSamples() const { return 8; }
		//! This method must be called right after the factory is called on a background light or the light will fail
		virtual void setBackground(const Background *bg) { background_ = bg; }
		//! Enable/disable entire light source
		bool lightEnabled() const { return light_enabled_;}
		bool castShadows() const { return cast_shadows_; }
		//! checks if the light can shoot caustic photons (photonmap integrator)
		bool shootsCausticP() const { return shoot_caustic_; }
		//! checks if the light can shoot diffuse photons (photonmap integrator)
		bool shootsDiffuseP() const { return shoot_diffuse_; }
		//! checks if the light is a photon-only light (only shoots photons, not illuminating)
		bool photonOnly() const { return photon_only_; }
		//! sets clampIntersect value to reduce noise at the expense of realism and inexact overall lighting
		void setClampIntersect(float clamp) { clamp_intersect_ = clamp; }
		Flags getFlags() const { return flags_; }
		std::string getName() const { return name_; }
		void setName(const std::string &name) { name_ = name; }

	protected:
		std::string name_;
		Flags flags_;
		const Background* background_ = nullptr;
		bool light_enabled_; //!< enable/disable light
		bool cast_shadows_; //!< enable/disable if the light should cast direct shadows
		bool shoot_caustic_; //!<enable/disable if the light can shoot caustic photons (photonmap integrator)
		bool shoot_diffuse_; //!<enable/disable if the light can shoot diffuse photons (photonmap integrator)
		bool photon_only_; //!<enable/disable if the light is a photon-only light (only shoots photons, not illuminating)
		float clamp_intersect_ = 0.f;	//!<trick to reduce light sampling noise at the expense of realism and inexact overall light. 0.f disables clamping
		Logger &logger_;
};

struct LSample
{
	explicit LSample(SurfacePoint *s_p = nullptr): sp_(s_p) {}
	float s_1_, s_2_; //<! 2d sample value for choosing a surface point on the light.
	float s_3_, s_4_; //<! 2d sample value for choosing an outgoing direction on the light (emitSample)
	float pdf_; //<! "standard" directional pdf from illuminated surface point for MC integration of direct lighting (illumSample)
	float dir_pdf_; //<! probability density for generating this sample direction (emitSample)
	float area_pdf_; //<! probability density for generating this sample point on light surface (emitSample)
	Rgb col_; //<! color of the generated sample
	Light::Flags flags_; //<! flags of the sampled light source
	SurfacePoint *sp_; //!< surface point on the light source, may only be complete enough to call other light methods with it!
};

END_YAFARAY

#endif // YAFARAY_LIGHT_H
