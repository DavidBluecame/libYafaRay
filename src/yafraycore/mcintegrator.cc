/****************************************************************************
 *		mcintegrator.h: A basic abstract integrator for MC sampling
 *		This is part of the yafray package
 *		Copyright (C) 2010  Rodrigo Placencia (DarkTide)
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

#include <core_api/mcintegrator.h>
#include <core_api/logging.h>
#include <core_api/renderpasses.h>
#include <core_api/material.h>
#include <core_api/scene.h>
#include <core_api/volume.h>
#include <core_api/session.h>
#include <core_api/light.h>
#include <yafraycore/scr_halton.h>
#include <yafraycore/spectrum.h>
#include <utilities/mcqmc.h>
#include <core_api/imagefilm.h>
#include <yafraycore/monitor.h>
#include <yafraycore/photon.h>

#ifdef __clang__
#define inline  // aka inline removal
#endif

__BEGIN_YAFRAY

#define allBSDFIntersect (BSDF_GLOSSY | BSDF_DIFFUSE | BSDF_DISPERSIVE | BSDF_REFLECT | BSDF_TRANSMIT);
#define loffsDelta 4567 //just some number to have different sequences per light...and it's a prime even...

inline color_t mcIntegrator_t::estimateAllDirectLight(renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, colorPasses_t &colorPasses) const
{
	color_t col;
	unsigned int loffs = 0;
	for(auto l = lights.begin(); l != lights.end(); ++l)
	{
		col += doLightEstimation(state, (*l), sp, wo, loffs, colorPasses);
		loffs++;
	}

	colorPasses.probe_mult(PASS_INT_SHADOW, 1.f / (float) loffs);

	return col;
}

inline color_t mcIntegrator_t::estimateOneDirectLight(renderState_t &state, const surfacePoint_t &sp, vector3d_t wo, int n, colorPasses_t &colorPasses) const
{
	int lightNum = lights.size();

	if(lightNum == 0) return color_t(0.f); //??? if you get this far the lights must be >= 1 but, what the hell... :)

	Halton hal2(2);

	hal2.setStart(imageFilm->getBaseSamplingOffset() + correlativeSampleNumber[state.threadID] - 1); //Probably with this change the parameter "n" is no longer necessary, but I will keep it just in case I have to revert back this change!
	int lnum = std::min((int)(hal2.getNext() * (float)lightNum), lightNum - 1);

	++correlativeSampleNumber[state.threadID];

	return doLightEstimation(state, lights[lnum], sp, wo, lnum, colorPasses) * lightNum;
}

inline color_t mcIntegrator_t::doLightEstimation(renderState_t &state, light_t *light, const surfacePoint_t &sp, const vector3d_t &wo, const unsigned int  &loffs, colorPasses_t &colorPasses) const
{
	color_t col(0.f);
	colorA_t colShadow(0.f), colShadowObjMask(0.f), colShadowMatMask(0.f), colDiffDir(0.f), colDiffNoShadow(0.f), colGlossyDir(0.f);
	bool shadowed;
	unsigned int l_offs = loffs * loffsDelta;
	const material_t *material = sp.material;
	ray_t lightRay;
	lightRay.from = sp.P;
	color_t lcol(0.f), scol;
	float lightPdf;
	float mask_obj_index = 0.f, mask_mat_index = 0.f;

	bool castShadows = light->castShadows() && material->getReceiveShadows();

	// handle lights with delta distribution, e.g. point and directional lights
	if(light->diracLight())
	{
		if(light->illuminate(sp, lcol, lightRay))
		{
			// ...shadowed...
			if(scene->shadowBiasAuto) lightRay.tmin = scene->shadowBias * std::max(1.f, vector3d_t(sp.P).length());
			else lightRay.tmin = scene->shadowBias;

			if(castShadows) shadowed = (trShad) ? scene->isShadowed(state, lightRay, sDepth, scol, mask_obj_index, mask_mat_index) : scene->isShadowed(state, lightRay, mask_obj_index, mask_mat_index);
			else shadowed = false;

			float angleLightNormal = (material->isFlat() ? 1.f : std::fabs(sp.N * lightRay.dir));	//If the material has the special attribute "isFlat()" then we will not multiply the surface reflection by the cosine of the angle between light and normal

			if(!shadowed || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
			{
				if(!shadowed && colorPasses.enabled(PASS_INT_SHADOW)) colShadow += color_t(1.f);

				color_t surfCol = material->eval(state, sp, wo, lightRay.dir, BSDF_ALL);
				color_t transmitCol = scene->volIntegrator->transmittance(state, lightRay);
				colorA_t tmpColNoShadow = surfCol * lcol * angleLightNormal * transmitCol;
				if(trShad && castShadows) lcol *= scol;

				if(colorPasses.enabled(PASS_INT_DIFFUSE) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
				{
					color_t tmpCol = material->eval(state, sp, wo, lightRay.dir, BSDF_DIFFUSE) * lcol * angleLightNormal * transmitCol;
					colDiffNoShadow += tmpColNoShadow;
					if(!shadowed) colDiffDir += tmpCol;
				}

				if(colorPasses.enabled(PASS_INT_GLOSSY) && state.raylevel == 0)
				{
					color_t tmpCol = material->eval(state, sp, wo, lightRay.dir, BSDF_GLOSSY, true) * lcol * angleLightNormal * transmitCol;
					if(!shadowed) colGlossyDir += tmpCol;
				}

				if(!shadowed) col += surfCol * lcol * angleLightNormal * transmitCol;
			}

			if(shadowed)
			{
				if((colorPasses.enabled(PASS_INT_MAT_INDEX_MASK_SHADOW))
				        && mask_mat_index == colorPasses.get_pass_mask_mat_index()) colShadowMatMask += color_t(1.f);

				if((colorPasses.enabled(PASS_INT_OBJ_INDEX_MASK_SHADOW))
				        && mask_obj_index == colorPasses.get_pass_mask_obj_index()) colShadowObjMask += color_t(1.f);
			}
		}
		colorPasses.probe_add(PASS_INT_SHADOW, colShadow, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_MAT_INDEX_MASK_SHADOW, colShadowMatMask, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_OBJ_INDEX_MASK_SHADOW, colShadowObjMask, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_DIFFUSE, colDiffDir, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_DIFFUSE_NO_SHADOW, colDiffNoShadow, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_GLOSSY, colGlossyDir, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_DEBUG_LIGHT_ESTIMATION_LIGHT_DIRAC, col, state.raylevel == 0);
	}
	else // area light and suchlike
	{
		Halton hal2(2);
		Halton hal3(3);
		int n = (int) ceilf(light->nSamples() * AA_light_sample_multiplier);
		if(state.rayDivision > 1) n = std::max(1, n / state.rayDivision);
		float invNS = 1.f / (float)n;
		unsigned int offs = n * state.pixelSample + state.samplingOffs + l_offs;
		bool canIntersect = light->canIntersect();
		color_t ccol(0.0);
		lSample_t ls;

		hal2.setStart(offs - 1);
		hal3.setStart(offs - 1);

		for(int i = 0; i < n; ++i)
		{
			// ...get sample val...
			ls.s1 = hal2.getNext();
			ls.s2 = hal3.getNext();

			if(light->illumSample(sp, ls, lightRay))
			{
				// ...shadowed...
				if(scene->shadowBiasAuto) lightRay.tmin = scene->shadowBias * std::max(1.f, vector3d_t(sp.P).length());
				else  lightRay.tmin = scene->shadowBias;

				if(castShadows) shadowed = (trShad) ? scene->isShadowed(state, lightRay, sDepth, scol, mask_obj_index, mask_mat_index) : scene->isShadowed(state, lightRay, mask_obj_index, mask_mat_index);
				else shadowed = false;

				if((!shadowed && ls.pdf > 1e-6f) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
				{
					color_t lsColNoShadow = ls.col;
					if(trShad && castShadows) ls.col *= scol;
					color_t transmitCol = scene->volIntegrator->transmittance(state, lightRay);
					ls.col *= transmitCol;
					color_t surfCol = material->eval(state, sp, wo, lightRay.dir, BSDF_ALL);

					if((!shadowed && ls.pdf > 1e-6f) && colorPasses.enabled(PASS_INT_SHADOW)) colShadow += color_t(1.f);

					float angleLightNormal = (material->isFlat() ? 1.f : std::fabs(sp.N * lightRay.dir));	//If the material has the special attribute "isFlat()" then we will not multiply the surface reflection by the cosine of the angle between light and normal

					if(canIntersect)
					{
						float mPdf = material->pdf(state, sp, wo, lightRay.dir, BSDF_GLOSSY | BSDF_DIFFUSE | BSDF_DISPERSIVE | BSDF_REFLECT | BSDF_TRANSMIT);
						if(mPdf > 1e-6f)
						{
							float l2 = ls.pdf * ls.pdf;
							float m2 = mPdf * mPdf;
							float w = l2 / (l2 + m2);

							if(colorPasses.enabled(PASS_INT_DIFFUSE) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
							{
								color_t tmpColNoLightColor = material->eval(state, sp, wo, lightRay.dir, BSDF_DIFFUSE) * angleLightNormal * w / ls.pdf;
								colDiffNoShadow += tmpColNoLightColor * lsColNoShadow;
								if((!shadowed && ls.pdf > 1e-6f)) colDiffDir += tmpColNoLightColor * ls.col;
							}
							if(colorPasses.enabled(PASS_INT_GLOSSY) && state.raylevel == 0)
							{
								color_t tmpCol = material->eval(state, sp, wo, lightRay.dir, BSDF_GLOSSY, true) * ls.col * angleLightNormal * w / ls.pdf;
								if((!shadowed && ls.pdf > 1e-6f)) colGlossyDir += tmpCol;
							}

							if((!shadowed && ls.pdf > 1e-6f)) ccol += surfCol * ls.col * angleLightNormal * w / ls.pdf;
						}
						else
						{
							if(colorPasses.enabled(PASS_INT_DIFFUSE) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
							{
								color_t tmpColNoLightColor = material->eval(state, sp, wo, lightRay.dir, BSDF_DIFFUSE) * angleLightNormal / ls.pdf;
								colDiffNoShadow += tmpColNoLightColor * lsColNoShadow;
								if((!shadowed && ls.pdf > 1e-6f)) colDiffDir += tmpColNoLightColor * ls.col;
							}

							if(colorPasses.enabled(PASS_INT_GLOSSY) && state.raylevel == 0)
							{
								color_t tmpCol = material->eval(state, sp, wo, lightRay.dir, BSDF_GLOSSY, true) * ls.col * angleLightNormal / ls.pdf;
								if((!shadowed && ls.pdf > 1e-6f)) colGlossyDir += tmpCol;
							}

							if((!shadowed && ls.pdf > 1e-6f)) ccol += surfCol * ls.col * angleLightNormal / ls.pdf;
						}
					}
					else
					{
						if(colorPasses.enabled(PASS_INT_DIFFUSE) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
						{
							color_t tmpColNoLightColor = material->eval(state, sp, wo, lightRay.dir, BSDF_DIFFUSE) * angleLightNormal / ls.pdf;
							colDiffNoShadow += tmpColNoLightColor * lsColNoShadow;
							if((!shadowed && ls.pdf > 1e-6f)) colDiffDir += tmpColNoLightColor * ls.col;
						}

						if(colorPasses.enabled(PASS_INT_GLOSSY) && state.raylevel == 0)
						{
							color_t tmpCol = material->eval(state, sp, wo, lightRay.dir, BSDF_GLOSSY, true) * ls.col * angleLightNormal / ls.pdf;
							if((!shadowed && ls.pdf > 1e-6f)) colGlossyDir += tmpCol;
						}

						if((!shadowed && ls.pdf > 1e-6f)) ccol += surfCol * ls.col * angleLightNormal / ls.pdf;
					}
				}

				if(shadowed || ls.pdf <= 1e-6f)
				{
					if((colorPasses.enabled(PASS_INT_MAT_INDEX_MASK_SHADOW))
					        && mask_mat_index == colorPasses.get_pass_mask_mat_index()) colShadowMatMask += color_t(1.f);

					if((colorPasses.enabled(PASS_INT_OBJ_INDEX_MASK_SHADOW))
					        && mask_obj_index == colorPasses.get_pass_mask_obj_index()) colShadowObjMask += color_t(1.f);
				}
			}
		}

		col += ccol * invNS;

		colorPasses.probe_add(PASS_INT_DEBUG_LIGHT_ESTIMATION_LIGHT_SAMPLING, ccol * invNS, state.raylevel == 0);

		colorPasses.probe_add(PASS_INT_SHADOW, colShadow * invNS, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_MAT_INDEX_MASK_SHADOW, colShadowMatMask * invNS, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_OBJ_INDEX_MASK_SHADOW, colShadowObjMask * invNS, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_DIFFUSE, colDiffDir * invNS, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_DIFFUSE_NO_SHADOW, colDiffNoShadow * invNS, state.raylevel == 0);
		colorPasses.probe_add(PASS_INT_GLOSSY, colGlossyDir * invNS, state.raylevel == 0);

		if(canIntersect) // sample from BSDF to complete MIS
		{
			color_t ccol2(0.f);

			if(colorPasses.enabled(PASS_INT_DIFFUSE) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
			{
				colDiffNoShadow = colorA_t(0.f);
				colDiffDir = colorA_t(0.f);
			}

			if(colorPasses.enabled(PASS_INT_GLOSSY) && state.raylevel == 0) colGlossyDir = colorA_t(0.f);

			hal2.setStart(offs - 1);
			hal3.setStart(offs - 1);

			for(int i = 0; i < n; ++i)
			{
				ray_t bRay;
				if(scene->rayMinDistAuto) bRay.tmin = scene->rayMinDist * std::max(1.f, vector3d_t(sp.P).length());
				else bRay.tmin = scene->rayMinDist;

				bRay.from = sp.P;

				float s1 = hal2.getNext();
				float s2 = hal3.getNext();
				float W = 0.f;

				sample_t s(s1, s2, BSDF_GLOSSY | BSDF_DIFFUSE | BSDF_DISPERSIVE | BSDF_REFLECT | BSDF_TRANSMIT);
				color_t surfCol = material->sample(state, sp, wo, bRay.dir, s, W);
				if(s.pdf > 1e-6f && light->intersect(bRay, bRay.tmax, lcol, lightPdf))
				{
					if(castShadows) shadowed = (trShad) ? scene->isShadowed(state, bRay, sDepth, scol, mask_obj_index, mask_mat_index) : scene->isShadowed(state, bRay, mask_obj_index, mask_mat_index);
					else shadowed = false;

					if((!shadowed && lightPdf > 1e-6f) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
					{
						if(trShad && castShadows) lcol *= scol;
						color_t transmitCol = scene->volIntegrator->transmittance(state, lightRay);
						lcol *= transmitCol;
						float lPdf = 1.f / lightPdf;
						float l2 = lPdf * lPdf;
						float m2 = s.pdf * s.pdf;
						float w = m2 / (l2 + m2);

						if(colorPasses.enabled(PASS_INT_DIFFUSE) || colorPasses.enabled(PASS_INT_DIFFUSE_NO_SHADOW))
						{
							color_t tmpCol = material->sample(state, sp, wo, bRay.dir, s, W) * lcol * w * W;
							colDiffNoShadow += tmpCol;
							if((!shadowed && lightPdf > 1e-6f) && ((s.sampledFlags & BSDF_DIFFUSE) == BSDF_DIFFUSE)) colDiffDir += tmpCol;
						}

						if(colorPasses.enabled(PASS_INT_GLOSSY) && state.raylevel == 0)
						{
							color_t tmpCol = material->sample(state, sp, wo, bRay.dir, s, W) * lcol * w * W;
							if((!shadowed && lightPdf > 1e-6f) && ((s.sampledFlags & BSDF_GLOSSY) == BSDF_GLOSSY)) colGlossyDir += tmpCol;
						}

						if((!shadowed && lightPdf > 1e-6f)) ccol2 += surfCol * lcol * w * W;
					}
				}
			}

			col += ccol2 * invNS;

			colorPasses.probe_add(PASS_INT_DEBUG_LIGHT_ESTIMATION_MAT_SAMPLING, ccol2 * invNS, state.raylevel == 0);
			colorPasses.probe_add(PASS_INT_DIFFUSE, colDiffDir * invNS, state.raylevel == 0);
			colorPasses.probe_add(PASS_INT_DIFFUSE_NO_SHADOW, colDiffNoShadow * invNS, state.raylevel == 0);
			colorPasses.probe_add(PASS_INT_GLOSSY, colGlossyDir * invNS, state.raylevel == 0);
		}
	}

	return col;
}

void mcIntegrator_t::causticWorker(photonMap_t *causticMap, int threadID, const scene_t *scene, unsigned int nCausPhotons, pdf1D_t *lightPowerD, int numLights, const std::string &integratorName, const std::vector<light_t *> &causLights, int causDepth, progressBar_t *pb, int pbStep, unsigned int &totalPhotonsShot)
{
	bool done = false;
	float s1, s2, s3, s4, s5, s6, s7, sL;
	float fNumLights = (float)numLights;
	float lightNumPdf, lightPdf;

	unsigned int curr = 0;
	unsigned int nCausPhotons_thread = 1 + ((nCausPhotons - 1) / scene->getNumThreadsPhotons());

	std::vector<photon_t> localCausticPhotons;

	surfacePoint_t sp1, sp2;
	surfacePoint_t *hit = &sp1, *hit2 = &sp2;
	ray_t ray;

	renderState_t state;
	state.cam = scene->getCamera();
	unsigned char userdata[USER_DATA_SIZE + 7];
	state.userdata = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes

	localCausticPhotons.clear();
	localCausticPhotons.reserve(nCausPhotons_thread);

	while(!done)
	{
		unsigned int haltoncurr = curr + nCausPhotons_thread * threadID;

		state.chromatic = true;
		state.wavelength = RI_S(haltoncurr);

		s1 = RI_vdC(haltoncurr);
		s2 = scrHalton(2, haltoncurr);
		s3 = scrHalton(3, haltoncurr);
		s4 = scrHalton(4, haltoncurr);

		sL = float(haltoncurr) / float(nCausPhotons);

		int lightNum = lightPowerD->DSample(sL, &lightNumPdf);

		if(lightNum >= numLights)
		{
			causticMap->mutx.lock();
			Y_ERROR << integratorName << ": lightPDF sample error! " << sL << "/" << lightNum << yendl;
			causticMap->mutx.unlock();
			return;
		}

		color_t pcol = causLights[lightNum]->emitPhoton(s1, s2, s3, s4, ray, lightPdf);
		ray.tmin = scene->rayMinDist;
		ray.tmax = -1.0;
		pcol *= fNumLights * lightPdf / lightNumPdf; //remember that lightPdf is the inverse of th pdf, hence *=...
		if(pcol.isBlack())
		{
			++curr;
			done = (curr >= nCausPhotons_thread);
			continue;
		}
		BSDF_t bsdfs = BSDF_NONE;
		int nBounces = 0;
		bool causticPhoton = false;
		bool directPhoton = true;
		const material_t *material = nullptr;
		const volumeHandler_t *vol = nullptr;

		while(scene->intersect(ray, *hit2))
		{
			if(std::isnan(pcol.R) || std::isnan(pcol.G) || std::isnan(pcol.B))
			{
				causticMap->mutx.lock();
				Y_WARNING << integratorName << ": NaN (photon color)" << yendl;
				causticMap->mutx.unlock();
				break;
			}
			color_t transm(1.f), vcol;
			// check for volumetric effects
			if(material)
			{
				if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(hit->Ng * ray.dir < 0)))
				{
					vol->transmittance(state, ray, vcol);
					transm = vcol;
				}
			}
			std::swap(hit, hit2);
			vector3d_t wi = -ray.dir, wo;
			material = hit->material;
			material->initBSDF(state, *hit, bsdfs);
			if(bsdfs & (BSDF_DIFFUSE | BSDF_GLOSSY))
			{
				//deposit caustic photon on surface
				if(causticPhoton)
				{
					photon_t np(wi, hit->P, pcol);
					localCausticPhotons.push_back(np);
				}
			}
			// need to break in the middle otherwise we scatter the photon and then discard it => redundant
			if(nBounces == causDepth) break;
			// scatter photon
			int d5 = 3 * nBounces + 5;
			//int d6 = d5 + 1;

			s5 = scrHalton(d5, haltoncurr);
			s6 = scrHalton(d5 + 1, haltoncurr);
			s7 = scrHalton(d5 + 2, haltoncurr);

			pSample_t sample(s5, s6, s7, BSDF_ALL_SPECULAR | BSDF_GLOSSY | BSDF_FILTER | BSDF_DISPERSIVE, pcol, transm);
			bool scattered = material->scatterPhoton(state, *hit, wi, wo, sample);
			if(!scattered) break; //photon was absorped.
			pcol = sample.color;
			// hm...dispersive is not really a scattering qualifier like specular/glossy/diffuse or the special case filter...
			causticPhoton = ((sample.sampledFlags & (BSDF_GLOSSY | BSDF_SPECULAR | BSDF_DISPERSIVE)) && directPhoton) ||
			                ((sample.sampledFlags & (BSDF_GLOSSY | BSDF_SPECULAR | BSDF_FILTER | BSDF_DISPERSIVE)) && causticPhoton);
			// light through transparent materials can be calculated by direct lighting, so still consider them direct!
			directPhoton = (sample.sampledFlags & BSDF_FILTER) && directPhoton;
			// caustic-only calculation can be stopped if:
			if(!(causticPhoton || directPhoton)) break;

			if(state.chromatic && (sample.sampledFlags & BSDF_DISPERSIVE))
			{
				state.chromatic = false;
				color_t wl_col;
				wl2rgb(state.wavelength, wl_col);
				pcol *= wl_col;
			}
			ray.from = hit->P;
			ray.dir = wo;
			ray.tmin = scene->rayMinDist;
			ray.tmax = -1.0;
			++nBounces;
		}
		++curr;
		if(curr % pbStep == 0)
		{
			pb->mutx.lock();
			pb->update();
			pb->mutx.unlock();
			if(scene->getSignals() & Y_SIG_ABORT) { return; }
		}
		done = (curr >= nCausPhotons_thread);
	}
	causticMap->mutx.lock();
	causticMap->appendVector(localCausticPhotons, curr);
	totalPhotonsShot += curr;
	causticMap->mutx.unlock();
}

bool mcIntegrator_t::createCausticMap()
{
	progressBar_t *pb;
	if(intpb) pb = intpb;
	else pb = new ConsoleProgressBar_t(80);

	if(photonMapProcessing == PHOTONS_LOAD)
	{
		pb->setTag("Loading caustic photon map from file...");
		const std::string filename = session.getPathImageOutput() + "_caustic.photonmap";
		Y_INFO << integratorName << ": Loading caustic photon map from: " << filename << ". If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << yendl;
		if(session.causticMap->load(filename))
		{
			Y_VERBOSE << integratorName << ": Caustic map loaded." << yendl;
			return true;
		}
		else
		{
			photonMapProcessing = PHOTONS_GENERATE_AND_SAVE;
			Y_WARNING << integratorName << ": photon map loading failed, changing to Generate and Save mode." << yendl;
		}
	}

	if(photonMapProcessing == PHOTONS_REUSE)
	{
		Y_INFO << integratorName << ": Reusing caustics photon map from memory. If it does not match the scene you could have crashes and/or incorrect renders, USE WITH CARE!" << yendl;
		if(session.causticMap->nPhotons() == 0)
		{
			photonMapProcessing = PHOTONS_GENERATE_ONLY;
			Y_WARNING << integratorName << ": One of the photon maps in memory was empty, they cannot be reused: changing to Generate mode." << yendl;
		}
		else return true;
	}

	session.causticMap->clear();
	session.causticMap->setNumPaths(0);
	session.causticMap->reserveMemory(nCausPhotons);
	session.causticMap->setNumThreadsPKDtree(scene->getNumThreadsPhotons());

	ray_t ray;
	std::vector<light_t *> causLights;

	for(unsigned int i = 0; i < lights.size(); ++i)
	{
		if(lights[i]->shootsCausticP())
		{
			causLights.push_back(lights[i]);
		}
	}

	int numLights = causLights.size();

	if(numLights > 0)
	{
		float lightNumPdf, lightPdf;
		float fNumLights = (float)numLights;
		float *energies = new float[numLights];
		for(int i = 0; i < numLights; ++i) energies[i] = causLights[i]->totalEnergy().energy();
		pdf1D_t *lightPowerD = new pdf1D_t(energies, numLights);

		Y_VERBOSE << integratorName << ": Light(s) photon color testing for caustics map:" << yendl;
		color_t pcol(0.f);

		for(int i = 0; i < numLights; ++i)
		{
			pcol = causLights[i]->emitPhoton(.5, .5, .5, .5, ray, lightPdf);
			lightNumPdf = lightPowerD->func[i] * lightPowerD->invIntegral;
			pcol *= fNumLights * lightPdf / lightNumPdf; //remember that lightPdf is the inverse of the pdf, hence *=...
			Y_VERBOSE << integratorName << ": Light [" << i + 1 << "] Photon col:" << pcol << " | lnpdf: " << lightNumPdf << yendl;
		}

		delete[] energies;

		int pbStep;
		Y_INFO << integratorName << ": Building caustics photon map..." << yendl;
		pb->init(128);
		pbStep = std::max(1U, nCausPhotons / 128);
		pb->setTag("Building caustics photon map...");

		unsigned int curr = 0;

		int nThreads = scene->getNumThreadsPhotons();

		nCausPhotons = std::max((unsigned int) nThreads, (nCausPhotons / nThreads) * nThreads); //rounding the number of diffuse photons so it's a number divisible by the number of threads (distribute uniformly among the threads). At least 1 photon per thread

		Y_PARAMS << integratorName << ": Shooting " << nCausPhotons << " photons across " << nThreads << " threads (" << (nCausPhotons / nThreads) << " photons/thread)" << yendl;

		if(nThreads >= 2)
		{
			std::vector<std::thread> threads;
			for(int i = 0; i < nThreads; ++i) threads.push_back(std::thread(&mcIntegrator_t::causticWorker, this, session.causticMap, i, scene, nCausPhotons, lightPowerD, numLights, std::ref(integratorName), causLights, causDepth, pb, pbStep, std::ref(curr)));
			for(auto &t : threads) t.join();
		}
		else
		{
			bool done = false;
			float s1, s2, s3, s4, s5, s6, s7, sL;
			surfacePoint_t sp1, sp2;
			surfacePoint_t *hit = &sp1, *hit2 = &sp2;

			renderState_t state;
			state.cam = scene->getCamera();
			unsigned char userdata[USER_DATA_SIZE + 7];
			state.userdata = (void *)(&userdata[7] - (((size_t)&userdata[7]) & 7));   // pad userdata to 8 bytes

			while(!done)
			{
				if(scene->getSignals() & Y_SIG_ABORT) { pb->done(); if(!intpb) delete pb; return false; }
				state.chromatic = true;
				state.wavelength = RI_S(curr);
				s1 = RI_vdC(curr);
				s2 = scrHalton(2, curr);
				s3 = scrHalton(3, curr);
				s4 = scrHalton(4, curr);

				sL = float(curr) / float(nCausPhotons);

				int lightNum = lightPowerD->DSample(sL, &lightNumPdf);

				if(lightNum >= numLights)
				{
					Y_ERROR << integratorName << ": lightPDF sample error! " << sL << "/" << lightNum << yendl;
					delete lightPowerD;
					return false;
				}

				color_t pcol = causLights[lightNum]->emitPhoton(s1, s2, s3, s4, ray, lightPdf);
				ray.tmin = scene->rayMinDist;
				ray.tmax = -1.0;
				pcol *= fNumLights * lightPdf / lightNumPdf; //remember that lightPdf is the inverse of th pdf, hence *=...
				if(pcol.isBlack())
				{
					++curr;
					done = (curr >= nCausPhotons);
					continue;
				}
				BSDF_t bsdfs = BSDF_NONE;
				int nBounces = 0;
				bool causticPhoton = false;
				bool directPhoton = true;
				const material_t *material = nullptr;
				const volumeHandler_t *vol = nullptr;

				while(scene->intersect(ray, *hit2))
				{
					if(std::isnan(pcol.R) || std::isnan(pcol.G) || std::isnan(pcol.B))
					{
						Y_WARNING << integratorName << ": NaN (photon color)" << yendl;
						break;
					}
					color_t transm(1.f), vcol;
					// check for volumetric effects
					if(material)
					{
						if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(hit->Ng * ray.dir < 0)))
						{
							vol->transmittance(state, ray, vcol);
							transm = vcol;
						}

					}
					std::swap(hit, hit2);
					vector3d_t wi = -ray.dir, wo;
					material = hit->material;
					material->initBSDF(state, *hit, bsdfs);
					if(bsdfs & (BSDF_DIFFUSE | BSDF_GLOSSY))
					{
						//deposit caustic photon on surface
						if(causticPhoton)
						{
							photon_t np(wi, hit->P, pcol);
							session.causticMap->pushPhoton(np);
							session.causticMap->setNumPaths(curr);
						}
					}
					// need to break in the middle otherwise we scatter the photon and then discard it => redundant
					if(nBounces == causDepth) break;
					// scatter photon
					int d5 = 3 * nBounces + 5;
					//int d6 = d5 + 1;

					s5 = scrHalton(d5, curr);
					s6 = scrHalton(d5 + 1, curr);
					s7 = scrHalton(d5 + 2, curr);

					pSample_t sample(s5, s6, s7, BSDF_ALL_SPECULAR | BSDF_GLOSSY | BSDF_FILTER | BSDF_DISPERSIVE, pcol, transm);
					bool scattered = material->scatterPhoton(state, *hit, wi, wo, sample);
					if(!scattered) break; //photon was absorped.
					pcol = sample.color;
					// hm...dispersive is not really a scattering qualifier like specular/glossy/diffuse or the special case filter...
					causticPhoton = ((sample.sampledFlags & (BSDF_GLOSSY | BSDF_SPECULAR | BSDF_DISPERSIVE)) && directPhoton) ||
					                ((sample.sampledFlags & (BSDF_GLOSSY | BSDF_SPECULAR | BSDF_FILTER | BSDF_DISPERSIVE)) && causticPhoton);
					// light through transparent materials can be calculated by direct lighting, so still consider them direct!
					directPhoton = (sample.sampledFlags & BSDF_FILTER) && directPhoton;
					// caustic-only calculation can be stopped if:
					if(!(causticPhoton || directPhoton)) break;

					if(state.chromatic && (sample.sampledFlags & BSDF_DISPERSIVE))
					{
						state.chromatic = false;
						color_t wl_col;
						wl2rgb(state.wavelength, wl_col);
						pcol *= wl_col;
					}
					ray.from = hit->P;
					ray.dir = wo;
					ray.tmin = scene->rayMinDist;
					ray.tmax = -1.0;
					++nBounces;
				}
				++curr;
				if(curr % pbStep == 0) pb->update();
				done = (curr >= nCausPhotons);
			}
		}

		pb->done();
		pb->setTag("Caustic photon map built.");
		Y_VERBOSE << integratorName << ": Done." << yendl;
		Y_INFO << integratorName << ": Shot " << curr << " caustic photons from " << numLights << " light(s)." << yendl;
		Y_VERBOSE << integratorName << ": Stored caustic photons: " << session.causticMap->nPhotons() << yendl;

		delete lightPowerD;

		if(session.causticMap->nPhotons() > 0)
		{
			pb->setTag("Building caustic photons kd-tree...");
			session.causticMap->updateTree();
			Y_VERBOSE << integratorName << ": Done." << yendl;
		}

		if(photonMapProcessing == PHOTONS_GENERATE_AND_SAVE)
		{
			pb->setTag("Saving caustic photon map to file...");
			std::string filename = session.getPathImageOutput() + "_caustic.photonmap";
			Y_INFO << integratorName << ": Saving caustic photon map to: " << filename << yendl;
			if(session.causticMap->save(filename)) Y_VERBOSE << integratorName << ": Caustic map saved." << yendl;
		}

		if(!intpb) delete pb;

	}
	else
	{
		Y_VERBOSE << integratorName << ": No caustic source lights found, skiping caustic map building..." << yendl;
	}

	return true;
}

inline color_t mcIntegrator_t::estimateCausticPhotons(renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo) const
{
	if(!session.causticMap->ready()) return color_t(0.f);

	foundPhoton_t *gathered = new foundPhoton_t[nCausSearch];//(foundPhoton_t *)alloca(nCausSearch * sizeof(foundPhoton_t));
	int nGathered = 0;

	float gRadiusSquare = causRadius * causRadius;

	nGathered = session.causticMap->gather(sp.P, gathered, nCausSearch, gRadiusSquare);

	gRadiusSquare = 1.f / gRadiusSquare;

	color_t sum(0.f);

	if(nGathered > 0)
	{
		const material_t *material = sp.material;
		color_t surfCol(0.f);
		float k = 0.f;
		const photon_t *photon;

		for(int i = 0; i < nGathered; ++i)
		{
			photon = gathered[i].photon;
			surfCol = material->eval(state, sp, wo, photon->direction(), BSDF_ALL);
			k = kernel(gathered[i].distSquare, gRadiusSquare);
			sum += surfCol * k * photon->color();
		}
		sum *= 1.f / (float(session.causticMap->nPaths()));
	}

	delete [] gathered;

	return sum;
}

inline void mcIntegrator_t::recursiveRaytrace(renderState_t &state, diffRay_t &ray, BSDF_t bsdfs, surfacePoint_t &sp, vector3d_t &wo, color_t &col, float &alpha, colorPasses_t &colorPasses, int additionalDepth) const
{
	const material_t *material = sp.material;
	spDifferentials_t spDiff(sp, ray);

	colorPasses_t tmpColorPasses = colorPasses;

	state.raylevel++;

	if(state.raylevel <= (rDepth + additionalDepth))
	{
		Halton hal2(2);
		Halton hal3(3);

		// dispersive effects with recursive raytracing:
		if((bsdfs & BSDF_DISPERSIVE) && state.chromatic)
		{
			state.includeLights = false; //debatable...
			int dsam = 8;
			int oldDivision = state.rayDivision;
			int oldOffset = state.rayOffset;
			float old_dc1 = state.dc1, old_dc2 = state.dc2;
			if(state.rayDivision > 1) dsam = std::max(1, dsam / oldDivision);
			state.rayDivision *= dsam;
			int branch = state.rayDivision * oldOffset;
			float d_1 = 1.f / (float)dsam;
			float ss1 = RI_S(state.pixelSample + state.samplingOffs);
			color_t dcol(0.f), vcol(1.f);
			vector3d_t wi;
			const volumeHandler_t *vol;
			diffRay_t refRay;
			float W = 0.f;

			if(tmpColorPasses.size() > 1) tmpColorPasses.reset_colors();

			for(int ns = 0; ns < dsam; ++ns)
			{
				state.wavelength = (ns + ss1) * d_1;
				state.dc1 = scrHalton(2 * state.raylevel + 1, branch + state.samplingOffs);
				state.dc2 = scrHalton(2 * state.raylevel + 2, branch + state.samplingOffs);
				if(oldDivision > 1)  state.wavelength = addMod1(state.wavelength, old_dc1);
				state.rayOffset = branch;
				++branch;
				sample_t s(0.5f, 0.5f, BSDF_REFLECT | BSDF_TRANSMIT | BSDF_DISPERSIVE);
				color_t mcol = material->sample(state, sp, wo, wi, s, W);

				if(s.pdf > 1.0e-6f && (s.sampledFlags & BSDF_DISPERSIVE))
				{
					state.chromatic = false;
					color_t wl_col;
					wl2rgb(state.wavelength, wl_col);
					refRay = diffRay_t(sp.P, wi, scene->rayMinDist);

					dcol += tmpColorPasses.probe_add(PASS_INT_TRANS, (color_t) integrate(state, refRay, tmpColorPasses, additionalDepth) * mcol * wl_col * W, state.raylevel == 1);

					state.chromatic = true;
				}
			}

			if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(sp.Ng * refRay.dir < 0)))
			{
				vol->transmittance(state, refRay, vcol);
				dcol *= vcol;
				//if(tmpColorPasses.size() > 1) tmpColorPasses *= vcol; //FIXME DAVID??
			}

			col += dcol * d_1;
			if(tmpColorPasses.size() > 1)
			{
				tmpColorPasses *= d_1;
				colorPasses += tmpColorPasses;
			}

			state.rayDivision = oldDivision;
			state.rayOffset = oldOffset;
			state.dc1 = old_dc1;
			state.dc2 = old_dc2;
		}

		// glossy reflection with recursive raytracing:
		if(bsdfs & BSDF_GLOSSY && state.raylevel < 20)
		{
			state.includeLights = true;
			int gsam = 8;
			int oldDivision = state.rayDivision;
			int oldOffset = state.rayOffset;
			float old_dc1 = state.dc1, old_dc2 = state.dc2;
			if(state.rayDivision > 1) gsam = std::max(1, gsam / oldDivision);
			state.rayDivision *= gsam;
			int branch = state.rayDivision * oldOffset;
			unsigned int offs = gsam * state.pixelSample + state.samplingOffs;
			float d_1 = 1.f / (float)gsam;
			color_t gcol(0.f), vcol(1.f);
			vector3d_t wi;
			const volumeHandler_t *vol;
			diffRay_t refRay;

			hal2.setStart(offs);
			hal3.setStart(offs);

			if(tmpColorPasses.size() > 1) tmpColorPasses.reset_colors();

			for(int ns = 0; ns < gsam; ++ns)
			{
				state.dc1 = scrHalton(2 * state.raylevel + 1, branch + state.samplingOffs);
				state.dc2 = scrHalton(2 * state.raylevel + 2, branch + state.samplingOffs);
				state.rayOffset = branch;
				++offs;
				++branch;

				float s1 = hal2.getNext();
				float s2 = hal3.getNext();

				if(material->getFlags() & BSDF_GLOSSY)
				{
					color_t mcol = 0.f;
					colorA_t integ = 0.f;
					if((material->getFlags() & BSDF_REFLECT) && !(material->getFlags() & BSDF_TRANSMIT))
					{
						float W = 0.f;

						sample_t s(s1, s2, BSDF_GLOSSY | BSDF_REFLECT);
						color_t mcol = material->sample(state, sp, wo, wi, s, W);
						colorA_t integ = 0.f;
						refRay = diffRay_t(sp.P, wi, scene->rayMinDist);
						if(diffRaysEnabled)
						{
							if(s.sampledFlags & BSDF_REFLECT) spDiff.reflectedRay(ray, refRay);
							else if(s.sampledFlags & BSDF_TRANSMIT) spDiff.refractedRay(ray, refRay, material->getMatIOR());
						}
						integ = (color_t)integrate(state, refRay, tmpColorPasses, additionalDepth);

						if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(sp.Ng * refRay.dir < 0)))
						{
							if(vol->transmittance(state, refRay, vcol)) integ *= vcol;
						}

						gcol += tmpColorPasses.probe_add(PASS_INT_GLOSSY_INDIRECT, (color_t)integ * mcol * W, state.raylevel == 1);
					}
					else if((material->getFlags() & BSDF_REFLECT) && (material->getFlags() & BSDF_TRANSMIT))
					{
						sample_t s(s1, s2, BSDF_GLOSSY | BSDF_ALL_GLOSSY);
						color_t mcol[2];
						float W[2];
						vector3d_t dir[2];

						mcol[0] = material->sample(state, sp, wo, dir, mcol[1], s, W);
						colorA_t integ = 0.f;

						if(s.sampledFlags & BSDF_REFLECT && !(s.sampledFlags & BSDF_DISPERSIVE))
						{
							refRay = diffRay_t(sp.P, dir[0], scene->rayMinDist);
							if(diffRaysEnabled) spDiff.reflectedRay(ray, refRay);
							integ = integrate(state, refRay, tmpColorPasses, additionalDepth);
							if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(sp.Ng * refRay.dir < 0)))
							{
								if(vol->transmittance(state, refRay, vcol)) integ *= vcol;
							}
							color_t colReflectFactor = mcol[0] * W[0];
							gcol += tmpColorPasses.probe_add(PASS_INT_TRANS, (color_t)integ * colReflectFactor, state.raylevel == 1);
						}

						if(s.sampledFlags & BSDF_TRANSMIT)
						{
							refRay = diffRay_t(sp.P, dir[1], scene->rayMinDist);
							if(diffRaysEnabled) spDiff.refractedRay(ray, refRay, material->getMatIOR());
							integ = integrate(state, refRay, tmpColorPasses, additionalDepth);
							if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(sp.Ng * refRay.dir < 0)))
							{
								if(vol->transmittance(state, refRay, vcol)) integ *= vcol;
							}

							color_t colTransmitFactor = mcol[1] * W[1];
							gcol += tmpColorPasses.probe_add(PASS_INT_GLOSSY_INDIRECT, (color_t)integ * colTransmitFactor, state.raylevel == 1);
							alpha = integ.A;
						}
					}
				}
			}

			col += gcol * d_1;

			if(tmpColorPasses.size() > 1)
			{
				tmpColorPasses *= d_1;
				colorPasses += tmpColorPasses;
			}

			state.rayDivision = oldDivision;
			state.rayOffset = oldOffset;
			state.dc1 = old_dc1;
			state.dc2 = old_dc2;
		}

		//...perfect specular reflection/refraction with recursive raytracing...
		if(bsdfs & (BSDF_SPECULAR | BSDF_FILTER) && state.raylevel < 20)
		{
			state.includeLights = true;
			bool reflect = false, refract = false;
			vector3d_t dir[2];
			color_t rcol[2], vcol;
			material->getSpecular(state, sp, wo, reflect, refract, dir, rcol);
			const volumeHandler_t *vol;
			if(reflect)
			{
				if(tmpColorPasses.size() > 1) tmpColorPasses.reset_colors();

				diffRay_t refRay(sp.P, dir[0], scene->rayMinDist);
				if(diffRaysEnabled) spDiff.reflectedRay(ray, refRay);
				color_t integ = integrate(state, refRay, tmpColorPasses, additionalDepth);

				if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(sp.Ng * refRay.dir < 0)))
				{
					if(vol->transmittance(state, refRay, vcol)) integ *= vcol;
				}

				col += colorPasses.probe_add(PASS_INT_REFLECT_PERFECT, (color_t)integ * rcol[0], state.raylevel == 1);
			}
			if(refract)
			{
				if(tmpColorPasses.size() > 1) tmpColorPasses.reset_colors();

				diffRay_t refRay;
				float transp_bias_factor = material->getTransparentBiasFactor();

				if(transp_bias_factor > 0.f)
				{
					bool transpbias_multiply_raydepth = material->getTransparentBiasMultiplyRayDepth();
					if(transpbias_multiply_raydepth) transp_bias_factor *= state.raylevel;
					refRay = diffRay_t(sp.P + dir[1] * transp_bias_factor, dir[1], scene->rayMinDist);
				}
				else refRay = diffRay_t(sp.P, dir[1], scene->rayMinDist);

				if(diffRaysEnabled) spDiff.refractedRay(ray, refRay, material->getMatIOR());
				colorA_t integ = integrate(state, refRay, tmpColorPasses, additionalDepth);

				if((bsdfs & BSDF_VOLUMETRIC) && (vol = material->getVolumeHandler(sp.Ng * refRay.dir < 0)))
				{
					if(vol->transmittance(state, refRay, vcol)) integ *= vcol;
				}

				col += colorPasses.probe_add(PASS_INT_REFRACT_PERFECT, (color_t)integ * rcol[1], state.raylevel == 1);

				alpha = integ.A;
			}
		}
	}
	--state.raylevel;
}

color_t mcIntegrator_t::sampleAmbientOcclusion(renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo) const
{
	color_t col(0.f), surfCol(0.f), scol(0.f);
	bool shadowed;
	const material_t *material = sp.material;
	ray_t lightRay;
	lightRay.from = sp.P;
	lightRay.dir = vector3d_t(0.f);
	float mask_obj_index = 0.f, mask_mat_index = 0.f;

	int n = aoSamples;//(int) ceilf(aoSamples*getSampleMultiplier());
	if(state.rayDivision > 1) n = std::max(1, n / state.rayDivision);

	unsigned int offs = n * state.pixelSample + state.samplingOffs;

	Halton hal2(2);
	Halton hal3(3);

	hal2.setStart(offs - 1);
	hal3.setStart(offs - 1);

	for(int i = 0; i < n; ++i)
	{
		float s1 = hal2.getNext();
		float s2 = hal3.getNext();

		if(state.rayDivision > 1)
		{
			s1 = addMod1(s1, state.dc1);
			s2 = addMod1(s2, state.dc2);
		}

		if(scene->shadowBiasAuto) lightRay.tmin = scene->shadowBias * std::max(1.f, vector3d_t(sp.P).length());
		else lightRay.tmin = scene->shadowBias;

		lightRay.tmax = aoDist;

		float W = 0.f;

		sample_t s(s1, s2, BSDF_GLOSSY | BSDF_DIFFUSE | BSDF_REFLECT);
		surfCol = material->sample(state, sp, wo, lightRay.dir, s, W);

		if(material->getFlags() & BSDF_EMIT)
		{
			col += material->emit(state, sp, wo) * s.pdf;
		}

		shadowed = (trShad) ? scene->isShadowed(state, lightRay, sDepth, scol, mask_obj_index, mask_mat_index) : scene->isShadowed(state, lightRay, mask_obj_index, mask_mat_index);

		if(!shadowed)
		{
			float cos = std::fabs(sp.N * lightRay.dir);
			if(trShad) col += aoCol * scol * surfCol * cos * W;
			else col += aoCol * surfCol * cos * W;
		}
	}

	return col / (float)n;
}

color_t mcIntegrator_t::sampleAmbientOcclusionPass(renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo) const
{
	color_t col(0.f), surfCol(0.f), scol(0.f);
	bool shadowed;
	const material_t *material = sp.material;
	ray_t lightRay;
	lightRay.from = sp.P;
	lightRay.dir = vector3d_t(0.f);
	float mask_obj_index = 0.f, mask_mat_index = 0.f;

	int n = aoSamples;//(int) ceilf(aoSamples*getSampleMultiplier());
	if(state.rayDivision > 1) n = std::max(1, n / state.rayDivision);

	unsigned int offs = n * state.pixelSample + state.samplingOffs;

	Halton hal2(2);
	Halton hal3(3);

	hal2.setStart(offs - 1);
	hal3.setStart(offs - 1);

	for(int i = 0; i < n; ++i)
	{
		float s1 = hal2.getNext();
		float s2 = hal3.getNext();

		if(state.rayDivision > 1)
		{
			s1 = addMod1(s1, state.dc1);
			s2 = addMod1(s2, state.dc2);
		}

		if(scene->shadowBiasAuto) lightRay.tmin = scene->shadowBias * std::max(1.f, vector3d_t(sp.P).length());
		else lightRay.tmin = scene->shadowBias;

		lightRay.tmax = aoDist;

		float W = 0.f;

		sample_t s(s1, s2, BSDF_GLOSSY | BSDF_DIFFUSE | BSDF_REFLECT);
		surfCol = material->sample(state, sp, wo, lightRay.dir, s, W);

		if(material->getFlags() & BSDF_EMIT)
		{
			col += material->emit(state, sp, wo) * s.pdf;
		}

		shadowed = scene->isShadowed(state, lightRay, mask_obj_index, mask_mat_index);

		if(!shadowed)
		{
			float cos = std::fabs(sp.N * lightRay.dir);
			//if(trShad) col += aoCol * scol * surfCol * cos * W;
			col += aoCol * surfCol * cos * W;
		}
	}

	return col / (float)n;
}


color_t mcIntegrator_t::sampleAmbientOcclusionPassClay(renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo) const
{
	color_t col(0.f), surfCol(0.f), scol(0.f);
	bool shadowed;
	const material_t *material = sp.material;
	ray_t lightRay;
	lightRay.from = sp.P;
	lightRay.dir = vector3d_t(0.f);
	float mask_obj_index = 0.f, mask_mat_index = 0.f;

	int n = aoSamples;
	if(state.rayDivision > 1) n = std::max(1, n / state.rayDivision);

	unsigned int offs = n * state.pixelSample + state.samplingOffs;

	Halton hal2(2);
	Halton hal3(3);

	hal2.setStart(offs - 1);
	hal3.setStart(offs - 1);

	for(int i = 0; i < n; ++i)
	{
		float s1 = hal2.getNext();
		float s2 = hal3.getNext();

		if(state.rayDivision > 1)
		{
			s1 = addMod1(s1, state.dc1);
			s2 = addMod1(s2, state.dc2);
		}

		if(scene->shadowBiasAuto) lightRay.tmin = scene->shadowBias * std::max(1.f, vector3d_t(sp.P).length());
		else lightRay.tmin = scene->shadowBias;

		lightRay.tmax = aoDist;

		float W = 0.f;

		sample_t s(s1, s2, BSDF_ALL);
		surfCol = material->sampleClay(state, sp, wo, lightRay.dir, s, W);
		s.pdf = 1.f;

		if(material->getFlags() & BSDF_EMIT)
		{
			col += material->emit(state, sp, wo) * s.pdf;
		}

		shadowed = scene->isShadowed(state, lightRay, mask_obj_index, mask_mat_index);

		if(!shadowed)
		{
			float cos = std::fabs(sp.N * lightRay.dir);
			//if(trShad) col += aoCol * scol * surfCol * cos * W;
			col += aoCol * surfCol * cos * W;
		}
	}

	return col / (float)n;
}

__END_YAFRAY
