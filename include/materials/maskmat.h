#pragma once

#ifndef Y_MASKMAT_H
#define Y_MASKMAT_H

#include <yafray_constants.h>
#include <yafraycore/nodematerial.h>
#include <core_api/color_ramp.h>

__BEGIN_YAFRAY

class texture_t;
class renderEnvironment_t;

class maskMat_t: public nodeMaterial_t
{
	public:
		maskMat_t(const material_t *m1, const material_t *m2, float thresh, visibility_t eVisibility = NORMAL_VISIBLE);
		virtual void initBSDF(const renderState_t &state, surfacePoint_t &sp, BSDF_t &bsdfTypes)const;
		virtual color_t eval(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, const vector3d_t &wi, BSDF_t bsdfs, bool force_eval = false)const;
		virtual color_t sample(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, vector3d_t &wi, sample_t &s, float &W)const;
		virtual float pdf(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo, const vector3d_t &wi, BSDF_t bsdfs)const;
		virtual bool isTransparent() const;
		virtual color_t getTransparency(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo)const;
		virtual void getSpecular(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo,
		                         bool &reflect, bool &refract, vector3d_t *const dir, color_t *const col)const;
		virtual color_t emit(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo)const;
		virtual float getAlpha(const renderState_t &state, const surfacePoint_t &sp, const vector3d_t &wo)const;
		static material_t *factory(paraMap_t &, std::list< paraMap_t > &, renderEnvironment_t &);

	protected:
		const material_t *mat1;
		const material_t *mat2;
		shaderNode_t *mask;
		float threshold;
		//const texture_t *mask;
};

__END_YAFRAY

#endif // Y_MASKMAT_H
