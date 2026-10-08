/* th10_port: the modules text_table.cpp needs, in place of FreeType's     */
/* include/freetype/config/ftmodule.h: the TrueType driver (and so TrueType */
/* collections), its SFNT tables and the anti-aliased renderer.            */
/* See ../../../README.md.                                                 */

FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )

/* EOF */
