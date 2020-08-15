/****************************************************************************
 *		directlight.cc: an integrator for direct lighting only
 *		This is part of the yafaray package
 *		Copyright (C) 2006  Mathias Wein (Lynx)
 *		Copyright (C) 2009  Rodrigo Placencia (DarkTide)
 *
 *		This library is free software; you can redistribute it and/or
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

#include <integrators/directlight.h>
#include <core_api/params.h>
#include <core_api/scene.h>
#include <core_api/imagesplitter.h>

__BEGIN_YAFRAY

directLighting_t::directLighting_t(bool transpShad, int shadowDepth, int rayDepth)
{
	type = SURFACE;
	causRadius = 0.25;
	causDepth = 10;
	nCausPhotons = 100000;
	nCausSearch = 100;
	trShad = transpShad;
	usePhotonCaustics = false;
	sDepth = shadowDepth;
	rDepth = rayDepth;
	integratorName = "DirectLight";
	integratorShortName = "DL";
}

bool directLighting_t::preprocess()
{
	bool success = true;
	std::stringstream set;
	gTimer.addEvent("prepass");
	gTimer.start("prepass");

	set << "Direct Light  ";

	if(trShad)
	{
		set << "ShadowDepth=" << sDepth << "  ";
	}
	set << "RayDepth=" << rDepth << "  ";

	if(useAmbientOcclusion)
	{
		set << "AO samples=" << aoSamples << " dist=" << aoDist << "  ";
	}

	background = scene->getBackground();
	lights = scene->lights;

	if(usePhotonCaustics)
	{
		success = createCausticMap();
		set << "\nCaustic photons=" << nCausPhotons << " search=" << nCausSearch << " radius=" << causRadius << " depth=" << causDepth << "  ";

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

colorA_t directLighting_t::integrate(renderState_t &state, diffRay_t &ray, colorPasses_t &colorPasses, int additionalDepth /*=0*/) const
{
	color_t col(0.0);
	float alpha;
	surfacePoint_t sp;
	void *o_udat = state.userdata;
	bool oldIncludeLights = state.includeLights;

	if(transpBackground) alpha = 0.0;
	else alpha = 1.0;

	// Shoot ray into scene

	if(scene->intersect(ray, sp)) // If it hits
	{
		unsigned char userdata[USER_DATA_SIZE];
		const material_t *material = sp.material;
		BSDF_t bsdfs;

		state.userdata = (void *) userdata;
		vector3d_t wo = -ray.dir;
		if(state.raylevel == 0) state.includeLights = true;

		material->initBSDF(state, sp, bsdfs);

		if(additionalDepth < material->getAdditionalDepth()) additionalDepth = material->getAdditionalDepth();


		if(bsdfs & BSDF_EMIT)
		{
			col += colorPasses.probe_set(PASS_INT_EMIT, material->emit(state, sp, wo), state.raylevel == 0);
		}

		if(bsdfs & BSDF_DIFFUSE)
		{
			col += estimateAllDirectLight(state, sp, wo, colorPasses);

			if(usePhotonCaustics)
			{
				if(AA_clamp_indirect > 0)
				{
					color_t tmpCol = estimateCausticPhotons(state, sp, wo);
					tmpCol.clampProportionalRGB(AA_clamp_indirect);
					col += colorPasses.probe_add(PASS_INT_INDIRECT, tmpCol, state.raylevel == 0);
				}
				else col += colorPasses.probe_add(PASS_INT_INDIRECT, estimateCausticPhotons(state, sp, wo), state.raylevel == 0);
			}

			if(useAmbientOcclusion) col += sampleAmbientOcclusion(state, sp, wo);
		}

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
	else // Nothing hit, return background if any
	{
		if(background && !transpRefractedBackground)
		{
			col += colorPasses.probe_set(PASS_INT_ENV, (*background)(ray, state), state.raylevel == 0);
		}
	}

	state.userdata = o_udat;
	state.includeLights = oldIncludeLights;

	color_t colVolTransmittance = scene->volIntegrator->transmittance(state, ray);
	color_t colVolIntegration = scene->volIntegrator->integrate(state, ray, colorPasses);

	if(transpBackground) alpha = std::max(alpha, 1.f - colVolTransmittance.R);

	colorPasses.probe_set(PASS_INT_VOLUME_TRANSMITTANCE, colVolTransmittance);
	colorPasses.probe_set(PASS_INT_VOLUME_INTEGRATION, colVolIntegration);

	col = (col * colVolTransmittance) + colVolIntegration;

	return colorA_t(col, alpha);
}

integrator_t *directLighting_t::factory(paraMap_t &params, renderEnvironment_t &render)
{
	bool transpShad = false;
	bool caustics = false;
	bool do_AO = false;
	int shadowDepth = 5;
	int raydepth = 5, cDepth = 10;
	int search = 100, photons = 500000;
	int AO_samples = 32;
	double cRad = 0.25;
	double AO_dist = 1.0;
	color_t AO_col(1.f);
	bool bg_transp = false;
	bool bg_transp_refract = false;
	std::string photon_maps_processing_str = "generate";

	params.getParam("raydepth", raydepth);
	params.getParam("transpShad", transpShad);
	params.getParam("shadowDepth", shadowDepth);
	params.getParam("caustics", caustics);
	params.getParam("photons", photons);
	params.getParam("caustic_mix", search);
	params.getParam("caustic_depth", cDepth);
	params.getParam("caustic_radius", cRad);
	params.getParam("do_AO", do_AO);
	params.getParam("AO_samples", AO_samples);
	params.getParam("AO_distance", AO_dist);
	params.getParam("AO_color", AO_col);
	params.getParam("bg_transp", bg_transp);
	params.getParam("bg_transp_refract", bg_transp_refract);
	params.getParam("photon_maps_processing", photon_maps_processing_str);

	directLighting_t *inte = new directLighting_t(transpShad, shadowDepth, raydepth);
	// caustic settings
	inte->usePhotonCaustics = caustics;
	inte->nCausPhotons = photons;
	inte->nCausSearch = search;
	inte->causDepth = cDepth;
	inte->causRadius = cRad;
	// AO settings
	inte->useAmbientOcclusion = do_AO;
	inte->aoSamples = AO_samples;
	inte->aoDist = AO_dist;
	inte->aoCol = AO_col;
	// Background settings
	inte->transpBackground = bg_transp;
	inte->transpRefractedBackground = bg_transp_refract;

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
		render.registerFactory("directlighting", directLighting_t::factory);
	}

}

__END_YAFRAY
