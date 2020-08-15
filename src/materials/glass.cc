/****************************************************************************
 * 			glass.cc: a dielectric material with dispersion, two trivial mats
 *      This is part of the yafray package
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
#include <yafray_constants.h>
#include <yafraycore/nodematerial.h>
#include <core_api/environment.h>
#include <yafraycore/spectrum.h>
#include <core_api/color_ramp.h>
#include <core_api/params.h>
#include <core_api/scene.h>

__BEGIN_YAFRAY

class glassMat_t: public nodeMaterial_t
{
	public:
		glassMat_t(float IOR, color_t filtC, const color_t &srcol, double disp_pow, bool fakeS, visibility_t eVisibility = NORMAL_VISIBLE);
		virtual void initBSDF(const renderState_t &state, surfacePoint_t &sp, unsigned int &bsdfTypes)const;
		virtual color_t eval(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, const vector3d_t &wl, BSDF_t bsdfs, bool force_eval = false)const {return color_t(0.0);}
		virtual color_t sample(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, vector3d_t &wi, sample_t &s, float &W)const;
		virtual float pdf(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, const vector3d_t &wi, BSDF_t bsdfs)const {return 0.f;}
		virtual bool isTransparent() const { return fakeShadow; }
		virtual color_t getTransparency(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo)const;
		virtual float getAlpha(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo)const;
		virtual void getSpecular(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo,
		                         bool &refl, bool &refr, vector3d_t *const dir, color_t *const col)const;
		virtual float getMatIOR() const;
		static material_t *factory(paraMap_t &, std::list< paraMap_t > &, renderEnvironment_t &);
		virtual color_t getGlossyColor(const renderState_t &state) const
		{
			nodeStack_t stack(state.userdata);
			return (mirColS ? mirColS->getColor(stack) : specRefCol);
		}
		virtual color_t getTransColor(const renderState_t &state) const
		{
			nodeStack_t stack(state.userdata);
			if(filterColS || filterCol.minimum() < .99f)	return (filterColS ? filterColS->getColor(stack) : filterCol);
			else
			{
				color_t tmpCol = beer_sigma_a;
				tmpCol.clampRGB01();
				return color_t(1.f) - tmpCol;
			}
		}
		virtual color_t getMirrorColor(const renderState_t &state) const
		{
			nodeStack_t stack(state.userdata);
			return (mirColS ? mirColS->getColor(stack) : specRefCol);
		}

	protected:
		shaderNode_t *bumpS = nullptr;
		shaderNode_t *mirColS = nullptr;
		shaderNode_t *filterColS = nullptr;
		shaderNode_t *iorS = nullptr;
		shaderNode_t *mWireFrameShader = nullptr;     //!< Shader node for wireframe shading (float)
		color_t filterCol, specRefCol;
		color_t beer_sigma_a;
		float ior;
		bool absorb = false, disperse = false, fakeShadow;
		BSDF_t tmFlags;
		float dispersion_power;
		float CauchyA, CauchyB;
};

glassMat_t::glassMat_t(float IOR, color_t filtC, const color_t &srcol, double disp_pow, bool fakeS, visibility_t eVisibility):
	filterCol(filtC), specRefCol(srcol), fakeShadow(fakeS), dispersion_power(disp_pow)
{
	mVisibility = eVisibility;
	ior = IOR;
	bsdfFlags = BSDF_ALL_SPECULAR;
	if(fakeS) bsdfFlags |= BSDF_FILTER;
	tmFlags = fakeS ? BSDF_FILTER | BSDF_TRANSMIT : BSDF_SPECULAR | BSDF_TRANSMIT;
	if(disp_pow > 0.0)
	{
		disperse = true;
		CauchyCoefficients(IOR, disp_pow, CauchyA, CauchyB);
		bsdfFlags |= BSDF_DISPERSIVE;
	}

	mVisibility = eVisibility;
}

void glassMat_t::initBSDF(const renderState_t &state, surfacePoint_t &sp, BSDF_t &bsdfTypes)const
{
	nodeStack_t stack(state.userdata);
	if(bumpS) evalBump(stack, state, sp, bumpS);

	//eval viewindependent nodes
	auto end = allViewindep.end();
	for(auto iter = allViewindep.begin(); iter != end; ++iter)(*iter)->eval(stack, state, sp);
	bsdfTypes = bsdfFlags;
}

#define matches(bits, flags) ((bits & (flags)) == (flags))

color_t glassMat_t::sample(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, vector3d_t &wi, sample_t &s, float &W)const
{
	nodeStack_t stack(state.userdata);
	if(!(s.flags & BSDF_SPECULAR) && !((s.flags & bsdfFlags & BSDF_DISPERSIVE) && state.chromatic))
	{
		s.pdf = 0.f;
		color_t scolor = color_t(0.f);
		float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
		applyWireFrame(scolor, wireFrameAmount, sp);
		return scolor;
	}
	vector3d_t refdir, N;
	bool outside = sp.Ng * wo > 0;
	float cos_wo_N = sp.N * wo;
	if(outside)	N = (cos_wo_N >= 0) ? sp.N : (sp.N - (1.00001 * cos_wo_N) * wo).normalize();
	else		N = (cos_wo_N <= 0) ? sp.N : (sp.N - (1.00001 * cos_wo_N) * wo).normalize();
	s.pdf = 1.f;

	// we need to sample dispersion
	if(disperse && state.chromatic)
	{
		float cur_ior = ior;

		if(iorS)
		{
			cur_ior += iorS->getScalar(stack);
		}

		float cur_cauchyA = CauchyA;
		float cur_cauchyB = CauchyB;

		if(iorS) CauchyCoefficients(cur_ior, dispersion_power, cur_cauchyA, cur_cauchyB);
		cur_ior = getIOR(state.wavelength, cur_cauchyA, cur_cauchyB);

		if(refract(N, wo, refdir, cur_ior))
		{
			float Kr, Kt;
			fresnel(wo, N, cur_ior, Kr, Kt);
			float pKr = 0.01 + 0.99 * Kr, pKt = 0.01 + 0.99 * Kt;
			if(!(s.flags & BSDF_SPECULAR) || s.s1 < pKt)
			{
				wi = refdir;
				s.pdf = (matches(s.flags, BSDF_SPECULAR | BSDF_REFLECT)) ? pKt : 1.f;
				s.sampledFlags = BSDF_DISPERSIVE | BSDF_TRANSMIT;
				W = 1.f;
				color_t scolor = (filterColS ? filterColS->getColor(stack) : filterCol); // * (Kt/std::fabs(sp.N*wi));
				float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
				applyWireFrame(scolor, wireFrameAmount, sp);
				return scolor;
			}
			else if(matches(s.flags, BSDF_SPECULAR | BSDF_REFLECT))
			{
				wi = wo;
				wi.reflect(N);
				s.pdf = pKr;
				s.sampledFlags = BSDF_SPECULAR | BSDF_REFLECT;
				W = 1.f;
				color_t scolor = (mirColS ? mirColS->getColor(stack) : specRefCol); // * (Kr/std::fabs(sp.N*wi));
				float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
				applyWireFrame(scolor, wireFrameAmount, sp);
				return scolor;
			}
		}
		else if(matches(s.flags, BSDF_SPECULAR | BSDF_REFLECT)) //total inner reflection
		{
			wi = wo;
			wi.reflect(N);
			s.sampledFlags = BSDF_SPECULAR | BSDF_REFLECT;
			W = 1.f;
			color_t scolor = 1.f; //color_t(1.f/std::fabs(sp.N*wi));
			float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
			applyWireFrame(scolor, wireFrameAmount, sp);
			return scolor;
		}
	}
	else // no dispersion calculation necessary, regardless of material settings
	{
		float cur_ior = ior;

		if(iorS)
		{
			cur_ior += iorS->getScalar(stack);
		}

		if(disperse && state.chromatic)
		{
			float cur_cauchyA = CauchyA;
			float cur_cauchyB = CauchyB;

			if(iorS) CauchyCoefficients(cur_ior, dispersion_power, cur_cauchyA, cur_cauchyB);
			cur_ior = getIOR(state.wavelength, cur_cauchyA, cur_cauchyB);
		}

		if(refract(N, wo, refdir, cur_ior))
		{
			float Kr, Kt;
			fresnel(wo, N, cur_ior, Kr, Kt);
			float pKr = 0.01 + 0.99 * Kr, pKt = 0.01 + 0.99 * Kt;
			if(s.s1 < pKt && matches(s.flags, tmFlags))
			{
				wi = refdir;
				s.pdf = pKt;
				s.sampledFlags = tmFlags;
				if(s.reverse)
				{
					s.pdf_back = s.pdf; //wrong...need to calc fresnel explicitly!
					s.col_back = (filterColS ? filterColS->getColor(stack) : filterCol);//*(Kt/std::fabs(sp.N*wo));
				}
				W = 1.f;
				color_t scolor = (filterColS ? filterColS->getColor(stack) : filterCol);//*(Kt/std::fabs(sp.N*wi));
				float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
				applyWireFrame(scolor, wireFrameAmount, sp);
				return scolor;
			}
			else if(matches(s.flags, BSDF_SPECULAR | BSDF_REFLECT)) //total inner reflection
			{
				wi = wo;
				wi.reflect(N);
				s.pdf = pKr;
				s.sampledFlags = BSDF_SPECULAR | BSDF_REFLECT;
				if(s.reverse)
				{
					s.pdf_back = s.pdf; //wrong...need to calc fresnel explicitly!
					s.col_back = (mirColS ? mirColS->getColor(stack) : specRefCol);// * (Kr/std::fabs(sp.N*wo));
				}
				W = 1.f;
				color_t scolor = (mirColS ? mirColS->getColor(stack) : specRefCol);// * (Kr/std::fabs(sp.N*wi));
				float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
				applyWireFrame(scolor, wireFrameAmount, sp);
				return scolor;
			}
		}
		else if(matches(s.flags, BSDF_SPECULAR | BSDF_REFLECT))//total inner reflection
		{
			wi = wo;
			wi.reflect(N);
			s.sampledFlags = BSDF_SPECULAR | BSDF_REFLECT;
			//color_t tir_col(1.f/std::fabs(sp.N*wi));
			if(s.reverse)
			{
				s.pdf_back = s.pdf;
				s.col_back = 1.f;//tir_col;
			}
			W = 1.f;
			color_t scolor = 1.f;//tir_col;
			float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
			applyWireFrame(scolor, wireFrameAmount, sp);
			return scolor;
		}
	}
	s.pdf = 0.f;
	return color_t(0.f);
}

color_t glassMat_t::getTransparency(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo)const
{
	nodeStack_t stack(state.userdata);
	vector3d_t N = FACE_FORWARD(sp.Ng, sp.N, wo);
	float Kr, Kt;
	fresnel(wo, N, (iorS ? iorS->getScalar(stack) : ior), Kr, Kt);
	color_t result = Kt * (filterColS ? filterColS->getColor(stack) : filterCol);

	float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
	applyWireFrame(result, wireFrameAmount, sp);
	return result;
}

float glassMat_t::getAlpha(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo)const
{
	nodeStack_t stack(state.userdata);
	float alpha = 1.0 - getTransparency(state, sp, wo).energy();
	if(alpha < 0.0f) alpha = 0.0f;

	float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
	applyWireFrame(alpha, wireFrameAmount, sp);
	return alpha;
}

void glassMat_t::getSpecular(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo,
                             bool &refl, bool &refr, vector3d_t *const dir, color_t *const col)const
{
	nodeStack_t stack(state.userdata);
	bool outside = sp.Ng * wo > 0;
	vector3d_t N;
	float cos_wo_N = sp.N * wo;
	if(outside)
	{
		N = (cos_wo_N >= 0) ? sp.N : (sp.N - (1.00001 * cos_wo_N) * wo).normalize();
	}
	else
	{
		N = (cos_wo_N <= 0) ? sp.N : (sp.N - (1.00001 * cos_wo_N) * wo).normalize();
	}
	//	vector3d_t N = FACE_FORWARD(sp.Ng, sp.N, wo);
	vector3d_t refdir;

	float cur_ior = ior;

	if(iorS)
	{
		cur_ior += iorS->getScalar(stack);
	}

	if(disperse && state.chromatic)
	{
		float cur_cauchyA = CauchyA;
		float cur_cauchyB = CauchyB;

		if(iorS) CauchyCoefficients(cur_ior, dispersion_power, cur_cauchyA, cur_cauchyB);
		cur_ior = getIOR(state.wavelength, cur_cauchyA, cur_cauchyB);
	}

	if(refract(N, wo, refdir, cur_ior))
	{
		float Kr, Kt;
		fresnel(wo, N, cur_ior, Kr, Kt);
		if(!state.chromatic || !disperse)
		{
			col[1] = Kt * (filterColS ? filterColS->getColor(stack) : filterCol);
			dir[1] = refdir;
			refr = true;
		}
		else refr = false; // in this case, we need to sample dispersion, i.e. not considered specular
		// accounting for fresnel reflection when leaving refractive material is a real performance
		// killer as rays keep bouncing inside objects and contribute little after few bounces, so limit we it:
		if(outside || state.raylevel < 3)
		{
			dir[0] = wo;
			dir[0].reflect(N);
			col[0] = (mirColS ? mirColS->getColor(stack) : specRefCol) * Kr;
			refl = true;
		}
		else refl = false;
	}
	else //total inner reflection
	{
		col[0] = mirColS ? mirColS->getColor(stack) : specRefCol;
		dir[0] = wo;
		dir[0].reflect(N);
		refl = true;
		refr = false;
	}

	float wireFrameAmount = (mWireFrameShader ? mWireFrameShader->getScalar(stack) * mWireFrameAmount : mWireFrameAmount);
	applyWireFrame(col, wireFrameAmount, sp);
}

float glassMat_t::getMatIOR() const
{
	return ior;
}

material_t *glassMat_t::factory(paraMap_t &params, std::list< paraMap_t > &paramList, renderEnvironment_t &render)
{
	double IOR = 1.4;
	double filt = 0.f;
	double disp_power = 0.0;
	color_t filtCol(1.f), absorp(1.f), srCol(1.f);
	const std::string *name = nullptr;
	bool fake_shad = false;
	std::string sVisibility = "normal";
	visibility_t visibility = NORMAL_VISIBLE;
	int mat_pass_index = 0;
	bool receive_shadows = true;
	int additionaldepth = 0;
	float samplingfactor = 1.f;
	float WireFrameAmount = 0.f;           //!< Wireframe shading amount
	float WireFrameThickness = 0.01f;      //!< Wireframe thickness
	float WireFrameExponent = 0.f;         //!< Wireframe exponent (0.f = solid, 1.f=linearly gradual, etc)
	color_t WireFrameColor = color_t(1.f); //!< Wireframe shading color

	params.getParam("IOR", IOR);
	params.getParam("filter_color", filtCol);
	params.getParam("transmit_filter", filt);
	params.getParam("mirror_color", srCol);
	params.getParam("dispersion_power", disp_power);
	params.getParam("fake_shadows", fake_shad);

	params.getParam("receive_shadows", receive_shadows);
	params.getParam("visibility", sVisibility);
	params.getParam("mat_pass_index",   mat_pass_index);
	params.getParam("additionaldepth",   additionaldepth);
	params.getParam("samplingfactor",   samplingfactor);

	params.getParam("wireframe_amount",  WireFrameAmount);
	params.getParam("wireframe_thickness",  WireFrameThickness);
	params.getParam("wireframe_exponent",  WireFrameExponent);
	params.getParam("wireframe_color",  WireFrameColor);

	if(sVisibility == "normal") visibility = NORMAL_VISIBLE;
	else if(sVisibility == "no_shadows") visibility = VISIBLE_NO_SHADOWS;
	else if(sVisibility == "shadow_only") visibility = INVISIBLE_SHADOWS_ONLY;
	else if(sVisibility == "invisible") visibility = INVISIBLE;
	else visibility = NORMAL_VISIBLE;

	glassMat_t *mat = new glassMat_t(IOR, filt * filtCol + color_t(1.f - filt), srCol, disp_power, fake_shad, visibility);

	mat->setMaterialIndex(mat_pass_index);
	mat->mReceiveShadows = receive_shadows;
	mat->additionalDepth = additionaldepth;

	mat->mWireFrameAmount = WireFrameAmount;
	mat->mWireFrameThickness = WireFrameThickness;
	mat->mWireFrameExponent = WireFrameExponent;
	mat->mWireFrameColor = WireFrameColor;

	mat->setSamplingFactor(samplingfactor);

	if(params.getParam("absorption", absorp))
	{
		double dist = 1.f;
		if(absorp.R < 1.f || absorp.G < 1.f || absorp.B < 1.f)
		{
			//deprecated method:
			color_t sigma(0.f);
			if(params.getParam("absorption_dist", dist))
			{
				const float maxlog = log(1e38);
				sigma.R = (absorp.R > 1e-38) ? -log(absorp.R) : maxlog;
				sigma.G = (absorp.G > 1e-38) ? -log(absorp.G) : maxlog;
				sigma.B = (absorp.B > 1e-38) ? -log(absorp.B) : maxlog;
				if(dist != 0.f) sigma *= 1.f / dist;
			}
			mat->absorb = true;
			mat->beer_sigma_a = sigma;
			mat->bsdfFlags |= BSDF_VOLUMETRIC;
			// creat volume handler (backwards compatibility)
			if(params.getParam("name", name))
			{
				paraMap_t map;
				map["type"] = std::string("beer");
				map["absorption_col"] = absorp;
				map["absorption_dist"] = parameter_t(dist);
				mat->volI = render.createVolumeH(*name, map);
				mat->bsdfFlags |= BSDF_VOLUMETRIC;
			}
		}
	}

	std::vector<shaderNode_t *> roots;
	std::map<std::string, shaderNode_t *> nodeList;

	// Prepare our node list
	nodeList["mirror_color_shader"] = nullptr;
	nodeList["bump_shader"] = nullptr;
	nodeList["filter_color_shader"] = nullptr;
	nodeList["IOR_shader"] = nullptr;
	nodeList["wireframe_shader"]    = nullptr;

	if(mat->loadNodes(paramList, render))
	{
		mat->parseNodes(params, roots, nodeList);
	}
	else Y_ERROR << "Glass: loadNodes() failed!" << yendl;

	mat->mirColS = nodeList["mirror_color_shader"];
	mat->bumpS = nodeList["bump_shader"];
	mat->filterColS = nodeList["filter_color_shader"];
	mat->iorS = nodeList["IOR_shader"];
	mat->mWireFrameShader    = nodeList["wireframe_shader"];

	// solve nodes order
	if(!roots.empty())
	{
		mat->solveNodesOrder(roots);
		std::vector<shaderNode_t *> colorNodes;
		if(mat->mirColS) mat->getNodeList(mat->mirColS, colorNodes);
		if(mat->filterColS) mat->getNodeList(mat->filterColS, colorNodes);
		if(mat->iorS) mat->getNodeList(mat->iorS, colorNodes);
		if(mat->mWireFrameShader)    mat->getNodeList(mat->mWireFrameShader, colorNodes);
		mat->filterNodes(colorNodes, mat->allViewdep, VIEW_DEP);
		mat->filterNodes(colorNodes, mat->allViewindep, VIEW_INDEP);
		if(mat->bumpS)
		{
			mat->getNodeList(mat->bumpS, mat->bumpNodes);
		}
	}
	mat->reqMem = mat->reqNodeMem;
	return mat;
}

/*====================================
a simple mirror mat
==================================*/

class mirrorMat_t: public material_t
{
	public:
		mirrorMat_t(color_t rCol, float refVal): ref(refVal)
		{
			if(ref > 1.0) ref = 1.0;
			refCol = rCol * refVal;
			bsdfFlags = BSDF_SPECULAR;
		}
		virtual void initBSDF(const renderState_t &state, surfacePoint_t &sp, unsigned int &bsdfTypes)const { bsdfTypes = bsdfFlags; }
		virtual color_t eval(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, const vector3d_t &wl, BSDF_t bsdfs, bool force_eval = false)const {return color_t(0.0);}
		virtual color_t sample(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, vector3d_t &wi, sample_t &s, float &W)const;
		virtual void getSpecular(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo,
		                         bool &refl, bool &refr, vector3d_t *const dir, color_t *const col)const;
		static material_t *factory(paraMap_t &, std::list< paraMap_t > &, renderEnvironment_t &);
	protected:
		color_t refCol;
		float ref;
};

color_t mirrorMat_t::sample(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, vector3d_t &wi, sample_t &s, float &W)const
{
	wi = reflect_dir(sp.N, wo);
	s.sampledFlags = BSDF_SPECULAR | BSDF_REFLECT;
	W = 1.f;
	return refCol * (1.f / std::fabs(sp.N * wi));
}

void mirrorMat_t::getSpecular(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo,
                              bool &refl, bool &refr, vector3d_t *const dir, color_t *const col)const
{
	col[0] = refCol;
	col[1] = color_t(1.0);
	vector3d_t N = FACE_FORWARD(sp.Ng, sp.N, wo);
	dir[0] = reflect_dir(N, wo);
	refl = true;
	refr = false;
}

material_t *mirrorMat_t::factory(paraMap_t &params, std::list< paraMap_t > &paramList, renderEnvironment_t &render)
{
	color_t col(1.0);
	float refl = 1.0;
	params.getParam("color", col);
	params.getParam("reflect", refl);
	return new mirrorMat_t(col, refl);
}


/*=============================================================
a "dummy" material, useful e.g. to keep photons from getting
stored on surfaces that don't affect the scene
=============================================================*/

class nullMat_t: public material_t
{
	public:
		nullMat_t() { }
		virtual void initBSDF(const renderState_t &state, surfacePoint_t &sp, unsigned int &bsdfTypes)const { bsdfTypes = BSDF_NONE; }
		virtual color_t eval(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, const vector3d_t &wl, BSDF_t bsdfs, bool force_eval = false)const {return color_t(0.0);}
		virtual color_t sample(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, vector3d_t &wi, sample_t &s, float &W)const;
		static material_t *factory(paraMap_t &, std::list< paraMap_t > &, renderEnvironment_t &);
};

color_t nullMat_t::sample(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, vector3d_t &wi, sample_t &s, float &W)const
{
	s.pdf = 0.f;
	W = 0.f;
	return color_t(0.f);
}

material_t *nullMat_t::factory(paraMap_t &, std::list< paraMap_t > &, renderEnvironment_t &)
{
	return new nullMat_t();
}

extern "C"
{
	YAFRAYPLUGIN_EXPORT void registerPlugin(renderEnvironment_t &render)
	{
		render.registerFactory("glass", glassMat_t::factory);
		render.registerFactory("mirror", mirrorMat_t::factory);
		render.registerFactory("null", nullMat_t::factory);
	}
}

__END_YAFRAY
