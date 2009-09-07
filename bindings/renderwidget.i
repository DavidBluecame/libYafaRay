%module yafqt

%include "cpointer.i"
%pointer_functions(float, floatp);
%pointer_functions(int, intp);
%pointer_functions(unsigned int, uintp);

%include "carrays.i"
%array_functions(float, floatArray);


%{
#include <yafray_constants.h>
#include <src/gui/yafqtapi.h>
%}

struct Settings {
    float* mem;
    bool autoSave;
    bool autoSaveAlpha;
    bool closeAfterFinish;
    const char* fileName;
};

void initGui();
int createRenderWidget(yafaray::yafrayInterface_t *interf, int xsize, int ysize, int bStartX, int bStartY, Settings settings);

