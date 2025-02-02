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

#ifndef YAFARAY_EXPORT_PYTHON_H
#define YAFARAY_EXPORT_PYTHON_H

#include "interface/interface.h"
#include <map>
#include <iostream>
#include <fstream>

BEGIN_YAFARAY

class Parameter;

class ExportPython: public Interface
{
	public:
		explicit ExportPython(const char *fname, ::yafaray_LoggerCallback_t logger_callback = nullptr, void *callback_data = nullptr, ::yafaray_DisplayConsole_t logger_display_console = YAFARAY_DISPLAY_CONSOLE_NORMAL);
		void createScene() noexcept override;
		int getSceneFilmWidth() const noexcept override { return 0; }
		int getSceneFilmHeight() const noexcept override { return 0; }
		void defineLayer() noexcept override;
		bool startGeometry() noexcept override;
		bool endGeometry() noexcept override;
		unsigned int getNextFreeId() noexcept override;
		bool endObject() noexcept override;
		bool addInstance(const char *base_object_name, const Matrix4 &obj_to_world) noexcept override;
		int  addVertex(double x, double y, double z) noexcept override; //!< add vertex to mesh; returns index to be used for addTriangle
		int  addVertex(double x, double y, double z, double ox, double oy, double oz) noexcept override; //!< add vertex with Orco to mesh; returns index to be used for addTriangle
		void addNormal(double nx, double ny, double nz) noexcept override; //!< add vertex normal to mesh; the vertex that will be attached to is the last one inserted by addVertex method
		bool addFace(int a, int b, int c) noexcept override;
		bool addFace(int a, int b, int c, int uv_a, int uv_b, int uv_c) noexcept override;
		int  addUv(float u, float v) noexcept override;
		bool smoothMesh(const char *name, double angle) noexcept override;
		void setCurrentMaterial(const char *name) noexcept override;
		Object *createObject(const char *name) noexcept override;
		Light *createLight(const char *name) noexcept override;
		Texture *createTexture(const char *name) noexcept override;
		const Material *createMaterial(const char *name) noexcept override;
		const Camera * createCamera(const char *name) noexcept override;
		const Background * createBackground(const char *name) noexcept override;
		Integrator *createIntegrator(const char *name) noexcept override;
		VolumeRegion *createVolumeRegion(const char *name) noexcept override;
		RenderView *createRenderView(const char *name) noexcept override;
		Image *createImage(const char *name) noexcept override;
		ImageOutput *createOutput(const char *name) noexcept override;
		void clearAll() noexcept override; //!< clear the whole environment + scene, i.e. free (hopefully) all memory.
		void clearOutputs() noexcept override { }
		void setupRender() noexcept override;
		void render(std::shared_ptr<ProgressBar> progress_bar) noexcept override; //!< render the scene...
		void setColorSpace(std::string color_space_string, float gamma_val) noexcept;

	protected:
		void writeParamMap(const ParamMap &param_map, int indent = 1) noexcept;
		void writeParamList(int indent) noexcept;
		static void writeMatrix(const std::string &name, const Matrix4 &m, std::ofstream &file) noexcept;
		static void writeParam(const std::string &name, const Parameter &param, std::ofstream &file, ColorSpace color_space, float gamma) noexcept;
		std::ofstream file_;
		std::string file_name_;
		std::string current_material_;
		int n_uvs_ = 0;
		unsigned int next_obj_ = 0;
		float gamma_ = 1.f;
		ColorSpace color_space_ = RawManualGamma;
};

END_YAFARAY

#endif // YAFARAY_EXPORT_PYTHON_H
