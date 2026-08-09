#include "app.h"
#include "sokol_app.h"

sapp_desc sokol_main(int argc, char *argv[])
{
	return App::make_desc(argc, argv);
}
