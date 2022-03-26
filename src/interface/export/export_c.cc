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

#include "interface/export/export_c.h"
#include "common/logger.h"
#include "scene/scene.h"
#include "geometry/matrix4.h"
#include "common/param.h"

BEGIN_YAFARAY

ExportC::ExportC(const char *fname, const ::yafaray_LoggerCallback_t logger_callback, void *callback_data, ::yafaray_DisplayConsole_t logger_display_console) : Interface(logger_callback, callback_data, logger_display_console), file_name_(std::string(fname))
{
	file_.open(file_name_.c_str());
	if(!file_.is_open())
	{
		logger_->logError("CExport: Couldn't open ", file_name_);
		return;
	}
	else logger_->logInfo("CExport: Writing scene to: ", file_name_);
	file_ << generateHeader();
}

std::string ExportC::generateHeader()
{
	std::stringstream ss;
	ss << "/* ANSI C89/C90 file generated by libYafaRay C Export */\n";
	ss << "/* To build use your favorite ANSI C compiler/linker, pointing to libYafaRay include/library files */\n";
	ss << "/* For example in Linux using GCC */\n";
	ss << "/* LD_LIBRARY_PATH=(path to folder with libyafaray libs) gcc -o libyafaray_example_executable -ansi -I(path to folder with libyafaray includes) -L(path to folder with libyafaray libs) (yafaray_scene_exported_source_file_name.c) -O0 -ggdb -lyafaray4 */\n";
	ss << "/* Note: no optimizations are needed for compiling this source file because it is libYafaRay itself which should be optimized for fastest execution. */\n";
	ss << "/*       Disabling compiler optimizations should help speeding up compilation of large scenes. */\n";
	ss << "/* To run the executable */\n";
	ss << "/* LD_LIBRARY_PATH=(path to folder with libyafaray libs) ./libyafaray_example_executable */\n\n";
	ss << "#include <yafaray_c_api.h>\n";
	ss << "#include <stddef.h>\n\n";
	ss << "void section_0(yafaray_Interface_t *yi)\n{\n";
	return ss.str();
}

std::string ExportC::generateMain() const
{
	std::stringstream ss;
	ss << "int main()\n";
	ss << "{\n";
	ss << "\t" << "yafaray_Interface_t *yi = yafaray_createInterface(YAFARAY_INTERFACE_FOR_RENDERING, NULL, NULL, NULL, YAFARAY_DISPLAY_CONSOLE_NORMAL);\n";
	ss << "\t" << "yafaray_setConsoleLogColorsEnabled(yi, YAFARAY_BOOL_TRUE);\n";
	ss << "\t" << "yafaray_setConsoleVerbosityLevel(yi, YAFARAY_LOG_LEVEL_DEBUG);\n\n";
	ss << generateSectionsCalls();
	ss << "\n\t" << "yafaray_render(yi, NULL, NULL, YAFARAY_DISPLAY_CONSOLE_NORMAL);\n";
	ss << "\n\t" << "yafaray_destroyInterface(yi);\n\n";
	ss << "\t" << "return 0;\n";
	ss << "}\n";
	return ss.str();
}

std::string ExportC::generateSectionsCalls() const
{
	std::stringstream ss;
	for(int section = 0; section < num_sections_; ++section)
	{
		ss << "\t" << "section_" << section << "(yi);\n";
	}
	return ss.str();
}

std::string ExportC::sectionSplit()
{
	section_num_lines_ = 0;
	std::stringstream ss;
	ss << "}\n\n";
	ss << "void section_" << num_sections_ << "(yafaray_Interface_t *yi)\n{\n";
	++num_sections_;
	return ss.str();
}

void ExportC::createScene() noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createScene(yi);\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n";
	section_num_lines_ += 2;
}

void ExportC::clearAll() noexcept
{
	if(logger_->isVerbose()) logger_->logVerbose("CExport: cleaning up...");
	if(file_.is_open())
	{
		file_.flush();
		file_.close();
	}
	params_->clear();
	nodes_params_.clear();
	cparams_ = params_.get();
	next_obj_ = 0;
}

void ExportC::defineLayer() noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_defineLayer(yi);\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
}

bool ExportC::startGeometry() noexcept { return true; }

bool ExportC::endGeometry() noexcept { return true; }

unsigned int ExportC::getNextFreeId() noexcept
{
	return ++next_obj_;
}

bool ExportC::endObject() noexcept
{
	file_ << "\t" << "yafaray_endObject(yi);\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	return true;
}

int ExportC::addVertex(double x, double y, double z) noexcept
{
	file_ << "\t" << "yafaray_addVertex(yi, " << x << ", " << y << ", " << z << ");\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return 0;
}

int ExportC::addVertex(double x, double y, double z, double ox, double oy, double oz) noexcept
{
	file_ << "\t" << "yafaray_addVertexWithOrco(yi, " << x << ", " << y << ", " << z << ", " << ox << ", " << oy << ", " << oz << ");\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return 0;
}

void ExportC::addNormal(double x, double y, double z) noexcept
{
	file_ << "\t" << "yafaray_addNormal(yi, " << x << ", " << y << ", " << z << ");\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
}

void ExportC::setCurrentMaterial(const char *name) noexcept
{
	const std::string name_str(name);
	if(name_str != current_material_) //need to set current material
	{
		file_ << "\t" << "yafaray_setCurrentMaterial(yi, \"" << name_str << "\");\n";
		current_material_ = name_str;
		++section_num_lines_;
	}
}

bool ExportC::addFace(int a, int b, int c) noexcept
{
	file_ << "\t" << "yafaray_addTriangle(yi, " << a << ", " << b << ", " << c << ");\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return true;
}

bool ExportC::addFace(int a, int b, int c, int uv_a, int uv_b, int uv_c) noexcept
{
	file_ << "\t" << "yafaray_addTriangleWithUv(yi, " << a << ", " << b << ", " << c << ", " << uv_a << ", " << uv_b << ", " << uv_c << ");\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return true;
}

int ExportC::addUv(float u, float v) noexcept
{
	file_ << "\t" << "yafaray_addUv(yi, " << u << ", " << v << ");\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return n_uvs_++;
}

bool ExportC::smoothMesh(const char *name, double angle) noexcept
{
	file_ << "\t" << "yafaray_smoothMesh(yi, \"" << name << "\", " << angle << ");\n\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return true;
}

void ExportC::writeMatrix(const std::string &name, const Matrix4 &m, std::ofstream &file) noexcept
{
	file << "\"" << name << "\", " <<
			m[0][0] << ", " << m[0][1] << ", " << m[0][2] << ", " << m[0][3] << ", " <<
			m[1][0] << ", " << m[1][1] << ", " << m[1][2] << ", " << m[1][3] << ", " <<
			m[2][0] << ", " << m[2][1] << ", " << m[2][2] << ", " << m[2][3] << ", " <<
			m[3][0] << ", " << m[3][1] << ", " << m[3][2] << ", " << m[3][3];
}

void ExportC::writeParam(const std::string &name, const Parameter &param, std::ofstream &file, ColorSpace color_space, float gamma) noexcept
{
	const Parameter::Type type = param.type();
	if(type == Parameter::Int)
	{
		int i = 0;
		param.getVal(i);
		file << "yafaray_paramsSetInt(yi, \"" << name << "\", " << i << ");\n";
	}
	else if(type == Parameter::Bool)
	{
		bool b = false;
		param.getVal(b);
		file << "yafaray_paramsSetBool(yi, \"" << name << "\", " << (b ? "YAFARAY_BOOL_TRUE" : "YAFARAY_BOOL_FALSE") << ");\n";
	}
	else if(type == Parameter::Float)
	{
		double f = 0.0;
		param.getVal(f);
		file << "yafaray_paramsSetFloat(yi, \"" << name << "\", " << f << ");\n";
	}
	else if(type == Parameter::String)
	{
		std::string s;
		param.getVal(s);
		if(!s.empty()) file << "yafaray_paramsSetString(yi, \"" << name << "\", \"" << s << "\");\n";
	}
	else if(type == Parameter::Vector)
	{
		Point3 p{0.f, 0.f, 0.f};
		param.getVal(p);
		file << "yafaray_paramsSetVector(yi, \"" << name << "\", " << p.x_ << ", " << p.y_ << ", " << p.z_ << ");\n";
	}
	else if(type == Parameter::Color)
	{
		Rgba c(0.f);
		param.getVal(c);
		c.colorSpaceFromLinearRgb(color_space, gamma);    //Color values are encoded to the desired color space before saving them to the XML file
		file << "yafaray_paramsSetColor(yi, \"" << name << "\", " << c.r_ << ", " << c.g_ << ", " << c.b_ << ", " << c.a_ << ");\n";
	}
	else if(type == Parameter::Matrix)
	{
		Matrix4 m;
		param.getVal(m);
		file << "yafaray_paramsSetMatrix(yi, ";
		writeMatrix(name, m, file);
		file << ", YAFARAY_BOOL_FALSE);\n";
	}
	else
	{
		std::cerr << "unknown parameter type!\n";
	}
}

bool ExportC::addInstance(const char *base_object_name, const Matrix4 &obj_to_world) noexcept
{
	file_ << "\t" << "yafaray_addInstance(yi, ";
	writeMatrix(base_object_name, obj_to_world, file_);
	file_ << ");\n";
	++section_num_lines_;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return true;
}

void ExportC::writeParamMap(const ParamMap &param_map, int indent) noexcept
{
	const std::string tabs(indent, '\t');
	for(const auto &param : param_map)
	{
		file_ << tabs;
		writeParam(param.first, param.second, file_, color_space_, gamma_);
		++section_num_lines_;
	}
}

void ExportC::writeParamList(int indent) noexcept
{
	const std::string tabs(indent, '\t');
	for(const auto &param : nodes_params_)
	{
		file_ << tabs << "yafaray_paramsPushList(yi);\n";
		writeParamMap(param, indent + 1);
	}
	file_ << tabs << "yafaray_paramsEndList(yi);\n";
	++section_num_lines_;
}

Light *ExportC::createLight(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createLight(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}

Texture *ExportC::createTexture(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createTexture(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}

Material *ExportC::createMaterial(const char *name) noexcept
{
	writeParamMap(*params_);
	writeParamList(1);
	params_->clear();
	nodes_params_.clear();
	file_ << "\t" << "yafaray_createMaterial(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}
const Camera * ExportC::createCamera(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createCamera(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}
Background *ExportC::createBackground(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createBackground(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}
Integrator *ExportC::createIntegrator(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createIntegrator(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}

VolumeRegion *ExportC::createVolumeRegion(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createVolumeRegion(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}

ImageOutput *ExportC::createOutput(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createOutput(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}

RenderView *ExportC::createRenderView(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createRenderView(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}

Image *ExportC::createImage(const char *name) noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createImage(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	return nullptr;
}

Object *ExportC::createObject(const char *name) noexcept
{
	n_uvs_ = 0;
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_createObject(yi, \"" << name << "\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
	if(section_num_lines_ >= section_max_lines_) file_ << sectionSplit();
	++next_obj_;
	return nullptr;
}

void ExportC::setupRender() noexcept
{
	writeParamMap(*params_);
	params_->clear();
	file_ << "\t" << "yafaray_setupRender(yi);\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n\n";
	section_num_lines_ += 2;
}

void ExportC::render(std::shared_ptr<ProgressBar> progress_bar) noexcept
{
	file_ << "\t" << "/* Creating image output */\n";
	file_ << "\t" << "yafaray_paramsSetString(yi, \"image_path\", \"./test01-output1.tga\");\n";
	file_ << "\t" << "yafaray_paramsSetString(yi, \"color_space\", \"sRGB\");\n";
	file_ << "\t" << "yafaray_paramsSetString(yi, \"badge_position\", \"top\");\n";
	file_ << "\t" << "yafaray_createOutput(yi, \"output1_tga\");\n";
	file_ << "\t" << "yafaray_paramsClearAll(yi);\n";
	file_ << "}\n\n";
	params_->clear();
	nodes_params_.clear();
	file_ << generateMain();
	file_.flush();
	file_.close();
}

void ExportC::setColorSpace(std::string color_space_string, float gamma_val) noexcept
{
	color_space_ = Rgb::colorSpaceFromName(color_space_string, ColorSpace::Srgb);
	gamma_ = gamma_val;
}

END_YAFARAY
