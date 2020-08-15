/****************************************************************************
 *		pathtracer.cc: A rather simple MC path integrator
 *		This is part of the yafaray package
 *		Copyright (C) 2006  Mathias Wein (Lynx)
 *		Copyright (C) 2009  Rodrigo Placencia (DarkTide)
 *
 *		This library is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU Lesser General Public
 *		License as published by the Free Software Foundation; either
 *		version 2.1 of the License, or (at your option) any later version.
 *
 *		This library is distributed in the hope that it will be useful,
 *		but WITHOUT ANY WARRANTY; without even the implied warranty of
 *		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *		Lesser General Public License for more details.
 *
 *		You should have received a copy of the GNU Lesser General Public
 *		License along with this library; if not, write to the Free Software
 *		Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <integrators/pathtracer.h>
#include <core_api/params.h>
#include <core_api/scene.h>
#include <core_api/imagesplitter.h>

__BEGIN_YAFRAY

pathIntegrator_t::pathIntegrator_t(bool transpShad, int shadowDepth)
{
	type = SURFACE;
	trShad = transpShad;
	sDepth = shadowDepth;
	causticType = PATH;
	rDepth = 6;
	maxBounces = 5;
	russianRouletteMinBounces = 0;
	nPaths = 64;
	invNPaths = 1.f / 64.f;
	no_recursive = false;
	integratorName = "PathTracer";
	integratorShortName = "PT";
}

bool pathIntegrator_t::preprocess()
{
	std::stringstream set;
	gTimer.addEvent("prepass");
	gTimer.start("prepass");

	background = scene->getBackground();
	lights = scene->lights;

	set << "Path Tracing  ";

	if(trShad)
	{
		set << "ShadowDepth=" << sDepth << "  ";
	}
	set << "RayDepth=" << rDepth << " npaths=" << nPaths << " bounces=" << maxBounces << " min_bounces=" << russianRouletteMinBounces << " ";

	bool success = true;
	traceCaustics = false;

	if(causticType == PHOTON || causticType == BOTH)
	{
		success = createCausticMap();
	}

	if(causticType == PATH)
	{
		set << "\nCaustics: Path" << " ";
	}
	else if(causticType == PHOTON)
	{
		set << "\nCaustics: Photons=" << nCausPhotons << " search=" << nCausSearch << " radius=" << causRadius << " depth=" << causDepth << "  ";
	}
	else if(causticType == BOTH)
	{
		set << "\nCaustics: Path + Photons=" << nCausPhotons << " search=" << nCausSearch << " radius=" << causRadius << " depth=" << causDepth << "  ";
	}

	if(causticType == BOTH || causticType == PATH) traceCaustics = true;

	if(causticType == BOTH || causticType == PHOTON)
	{
		if(photonMapProcessing == PHOTONS_LOAD)
		{
			set << " (loading photon maps from file)";
		}
		else if(photonMapProcessing == PHOTONS_REUSE)
		{
			set << " (reusing photon maps from memory)";
		}
		else if(photonMapProcessing == PHOTONS_GENERATE_AND_SAVE) set << " (saving photon maps to file)";
	}

	gTimer.stop("prepass");
	Y_INFO << integratorName << ": Photonmap building time: " << std::fixed << std::setprecision(1) << gTimer.getTime("prepass") << "s" << " (" << scene->getNumThreadsPhotons() << " thread(s))" << yendl;

	set << "| photon maps: " << std::fixed << std::setprecision(1) << gTimer.getTime("prepass") << "s" << " [" << scene->getNumThreadsPhotons() << " thread(s)]";

	yafLog.appendRenderSettings(set.str());

	for(std::string line; std::getline(set, line, '\n');) Y_VERBOSE << line << yendl;

	return success;
}

colorA_t pathIntegrator_t::integrate(renderState_t &state, diffRay_t &ray, colorPasses_t &colorPasses, int additionalDepth /*=0*/) const
{
	static int calls = 0;
	++calls;
	color_t col(0.0);
	float alpha;
	surfacePoint_t sp;
	void *o_udat = state.userdata;
	float W = 0.f;

	if(transpBackground) alpha = 0.0;
	else alpha = 1.0;

	colorPasses_t tmpColorPasses = colorPasses;

	//shoot ray into scene
	if(scene->intersect(ray, sp))
	{
		// if camera ray initialize sampling offset:
		if(state.raylevel == 0)
		{
			state.includeLights = true;
			//...
		}
		unsigned char userdata[USER_DATA_SIZE + 7];
		userdata[0] = 0;
		state.userdata = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes
		BSDF_t bsdfs;

		const material_t *material = sp.material;
		material->initBSDF(state, sp, bsdfs);
		vector3d_t wo = -ray.dir;
		const volumeHandler_t *vol;
		color_t vcol(0.f);

		random_t &prng = *(state.prng);

		if(additionalDepth < material->getAdditionalDepth()) additionalDepth = material->getAdditionalDepth();

		// contribution of light emitting surfaces
		if(bsdfs & BSDF_EMIT) col += colorPasses.probe_add(PASS_INT_EMIT, material->emit(state, sp, wo), state.raylevel == 0);

		if(bsdfs & BSDF_DIFFUSE)
		{
			col += estimateAllDirectLight(state, sp, wo, colorPasses);

			if(causticType == PHOTON || causticType == BOTH)
			{
				if(AA_clamp_indirect > 0)
				{
					color_t tmpCol = estimateCausticPhotons(state, sp, wo);
					tmpCol.clampProportionalRGB(AA_clamp_indirect);
					col += colorPasses.probe_set(PASS_INT_INDIRECT, tmpCol, state.raylevel == 0);
				}
				else col += colorPasses.probe_set(PASS_INT_INDIRECT, estimateCausticPhotons(state, sp, wo), state.raylevel == 0);
			}
		}

		// path tracing:
		// the first path segment is "unrolled" from the loop because for the spot the camera hit
		// we do things slightly differently (e.g. may not sample specular, need not to init BSDF anymore,
		// have more efficient ways to compute samples...)

		bool was_chromatic = state.chromatic;
		BSDF_t path_flags = no_recursive ? BSDF_ALL : (BSDF_DIFFUSE);

		if(bsdfs & path_flags)
		{
			color_t pathCol(0.0), wl_col;
			path_flags |= (BSDF_DIFFUSE | BSDF_REFLECT | BSDF_TRANSMIT);
			int nSamples = std::max(1, nPaths / state.rayDivision);
			for(int i = 0; i < nSamples; ++i)
			{
				void *first_udat = state.userdata;
				unsigned char userdata[USER_DATA_SIZE + 7];
				void *n_udat = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes
				unsigned int offs = nPaths * state.pixelSample + state.samplingOffs + i; // some redunancy here...
				color_t throughput(1.0);
				color_t lcol, scol;
				surfacePoint_t sp1 = sp, sp2;
				surfacePoint_t *hit = &sp1, *hit2 = &sp2;
				vector3d_t pwo = wo;
				ray_t pRay;

				state.chromatic = was_chromatic;
				if(was_chromatic) state.wavelength = RI_S(offs);
				//this mat already is initialized, just sample (diffuse...non-specular?)
				float s1 = RI_vdC(offs);
				float s2 = scrHalton(2, offs);
				if(state.rayDivision > 1)
				{
					s1 = addMod1(s1, state.dc1);
					s2 = addMod1(s2, state.dc2);
				}
				// do proper sampling now...
				sample_t s(s1, s2, path_flags);
				scol = material->sample(state, sp, pwo, pRay.dir, s, W);

				scol *= W;
				throughput = scol;
				state.includeLights = false;

				pRay.tmin = scene->rayMinDist;
				pRay.tmax = -1.0;
				pRay.from = sp.P;

				if(!scene->intersect(pRay, *hit)) continue; //hit background

				state.userdata = n_udat;
				const material_t *p_mat = hit->material;
				BSDF_t matBSDFs;
				p_mat->initBSDF(state, *hit, matBSDFs);
				if(s.sampledFlags != BSDF_NONE) pwo = -pRay.dir; //Fix for white dots in path tracing with shiny diffuse with transparent PNG texture and transparent shadows, especially in Win32, (precision?). Sometimes the first sampling does not take place and pRay.dir is not initialized, so before this change when that happened pwo = -pRay.dir was getting a random non-initialized value! This fix makes that, if the first sample fails for some reason, pwo is not modified and the rest of the sampling continues with the same pwo value. FIXME: Question: if the first sample fails, should we continue as now or should we exit the loop with the "continue" command?
				lcol = estimateOneDirectLight(state, *hit, pwo, offs, tmpColorPasses);
				if(matBSDFs & BSDF_EMIT) lcol += colorPasses.probe_add(PASS_INT_EMIT, p_mat->emit(state, *hit, pwo), state.raylevel == 0);

				pathCol += lcol * throughput;

				bool caustic = false;

				for(int depth = 1; depth < maxBounces; ++depth)
				{
					int d4 = 4 * depth;
					s.s1 = scrHalton(d4 + 3, offs); //ourRandom();//
					s.s2 = scrHalton(d4 + 4, offs); //ourRandom();//

					if(state.rayDivision > 1)
					{
						s1 = addMod1(s1, state.dc1);
						s2 = addMod1(s2, state.dc2);
					}

					s.flags = BSDF_ALL;

					scol = p_mat->sample(state, *hit, pwo, pRay.dir, s, W);
					scol *= W;

					if(scol.isBlack()) break;

					throughput *= scol;
					caustic = traceCaustics && (s.sampledFlags & (BSDF_SPECULAR | BSDF_GLOSSY | BSDF_FILTER));
					state.includeLights = caustic;

					pRay.tmin = scene->rayMinDist;
					pRay.tmax = -1.0;
					pRay.from = hit->P;

					if(!scene->intersect(pRay, *hit2)) //hit background
					{
						if((caustic && background && background->hasIBL() && background->shootsCaustic()))
						{
							pathCol += throughput * (*background)(pRay, state, true);
						}
						break;
					}

					std::swap(hit, hit2);
					p_mat = hit->material;
					p_mat->initBSDF(state, *hit, matBSDFs);
					pwo = -pRay.dir;

					if(matBSDFs & BSDF_DIFFUSE) lcol = estimateOneDirectLight(state, *hit, pwo, offs, tmpColorPasses);
					else lcol = color_t(0.f);

					if((matBSDFs & BSDF_VOLUMETRIC) && (vol = p_mat->getVolumeHandler(hit->N * pwo < 0)))
					{
						if(vol->transmittance(state, pRay, vcol)) throughput *= vcol;
					}

					// Russian roulette for terminating paths with low probability
					if(depth > russianRouletteMinBounces)
					{
						float random_value = prng();
						float probability = throughput.maximum();
						if(probability <= 0.f || probability < random_value) break;
						throughput *= 1.f / probability;
					}

					if((matBSDFs & BSDF_EMIT) && caustic) lcol += colorPasses.probe_add(PASS_INT_EMIT, p_mat->emit(state, *hit, pwo), state.raylevel == 0);

					pathCol += lcol * throughput;
				}
				state.userdata = first_udat;

			}
			col += pathCol / nSamples;
		}
		//reset chromatic state:
		state.chromatic = was_chromatic;

		recursiveRaytrace(state, ray, bsdfs, sp, wo, col, alpha, colorPasses, additionalDepth);

		if(colorPasses.size() > 1 && state.raylevel == 0)
		{
			generateCommonRenderPasses(colorPasses, state, sp, ray);

			if(colorPasses.enabled(PASS_INT_AO))
			{
				colorPasses(PASS_INT_AO) = sampleAmbientOcclusionPass(state, sp, wo);
			}

			if(colorPasses.enabled(PASS_INT_AO_CLAY))
			{
				colorPasses(PASS_INT_AO_CLAY) = sampleAmbientOcclusionPassClay(state, sp, wo);
			}
		}

		if(transpRefractedBackground)
		{
			float m_alpha = material->getAlpha(state, sp, wo);
			alpha = m_alpha + (1.f - m_alpha) * alpha;
		}
		else alpha = 1.0;
	}
	else //nothing hit, return background
	{
		if(background && !transpRefractedBackground)
		{
			col += colorPasses.probe_set(PASS_INT_ENV, (*background)(ray, state), state.raylevel == 0);
		}
	}

	state.userdata = o_udat;

	color_t colVolTransmittance = scene->volIntegrator->transmittance(state, ray);
	color_t colVolIntegration = scene->volIntegrator->integrate(state, ray, colorPasses);

	if(transpBackground) alpha = std::max(alpha, 1.f - colVolTransmittance.R);

	colorPasses.probe_set(PASS_INT_VOLUME_TRANSMITTANCE, colVolTransmittance);
	colorPasses.probe_set(PASS_INT_VOLUME_INTEGRATION, colVolIntegration);

	col = (col * colVolTransmittance) + colVolIntegration;

	return colorA_t(col, alpha);
}

integrator_t *pathIntegrator_t::factory(paraMap_t &params, renderEnvironment_t &render)
{
	bool transpShad = false, noRec = false;
	int shadowDepth = 5;
	int path_samples = 32;
	int bounces = 3;
	int russian_roulette_min_bounces = 0;
	int raydepth = 5;
	const std::string *cMethod = 0;
	bool do_AO = false;
	int AO_samples = 32;
	double AO_dist = 1.0;
	color_t AO_col(1.f);
	bool bg_transp = false;
	bool bg_transp_refract = false;
	std::string photon_maps_processing_str = "generate";

	params.getParam("raydepth", raydepth);
	params.getParam("transpShad", transpShad);
	params.getParam("shadowDepth", shadowDepth);
	params.getParam("path_samples", path_samples);
	params.getParam("bounces", bounces);
	params.getParam("russian_roulette_min_bounces", russian_roulette_min_bounces);
	params.getParam("no_recursive", noRec);
	params.getParam("bg_transp", bg_transp);
	params.getParam("bg_transp_refract", bg_transp_refract);
	params.getParam("do_AO", do_AO);
	params.getParam("AO_samples", AO_samples);
	params.getParam("AO_distance", AO_dist);
	params.getParam("AO_color", AO_col);
	params.getParam("photon_maps_processing", photon_maps_processing_str);

	pathIntegrator_t *inte = new pathIntegrator_t(transpShad, shadowDepth);
	if(params.getParam("caustic_type", cMethod))
	{
		bool usePhotons = false;
		if(*cMethod == "photon") { inte->causticType = PHOTON; usePhotons = true; }
		else if(*cMethod == "both") { inte->causticType = BOTH; usePhotons = true; }
		else if(*cMethod == "none") inte->causticType = NONE;
		if(usePhotons)
		{
			double cRad = 0.25;
			int cDepth = 10, search = 100, photons = 500000;
			params.getParam("photons", photons);
			params.getParam("caustic_mix", search);
			params.getParam("caustic_depth", cDepth);
			params.getParam("caustic_radius", cRad);
			inte->nCausPhotons = photons;
			inte->nCausSearch = search;
			inte->causDepth = cDepth;
			inte->causRadius = cRad;
		}
	}
	inte->rDepth = raydepth;
	inte->nPaths = path_samples;
	inte->invNPaths = 1.f / (float)path_samples;
	inte->maxBounces = bounces;
	inte->russianRouletteMinBounces = russian_roulette_min_bounces;
	inte->no_recursive = noRec;
	// Background settings
	inte->transpBackground = bg_transp;
	inte->transpRefractedBackground = bg_transp_refract;
	// AO settings
	inte->useAmbientOcclusion = do_AO;
	inte->aoSamples = AO_samples;
	inte->aoDist = AO_dist;
	inte->aoCol = AO_col;

	if(photon_maps_processing_str == "generate-save") inte->photonMapProcessing = PHOTONS_GENERATE_AND_SAVE;
	else if(photon_maps_processing_str == "load") inte->photonMapProcessing = PHOTONS_LOAD;
	else if(photon_maps_processing_str == "reuse-previous") inte->photonMapProcessing = PHOTONS_REUSE;
	else inte->photonMapProcessing = PHOTONS_GENERATE_ONLY;

	return inte;
}

extern "C"
{

	YAFRAYPLUGIN_EXPORT void registerPlugin(renderEnvironment_t &render)
	{
		render.registerFactory("pathtracing", pathIntegrator_t::factory);
	}

}

__END_YAFRAY
