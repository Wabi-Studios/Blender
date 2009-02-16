/**
 * $Id$
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * Contributor(s): Blender Foundation, 2002-2009
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include <string.h>
#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "DNA_image_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_space_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_image.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_node.h"
#include "BKE_packedFile.h"
#include "BKE_report.h"
#include "BKE_screen.h"

#include "BLI_arithb.h"
#include "BLI_blenlib.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "RE_pipeline.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_types.h"

#include "ED_image.h"
#include "ED_screen.h"
#include "ED_space_api.h"
#include "ED_uvedit.h"

#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "WM_api.h"
#include "WM_types.h"

#include "image_intern.h"

/******************** view navigation utilities *********************/

static void sima_zoom_set(SpaceImage *sima, ARegion *ar, float zoom)
{
	float oldzoom= sima->zoom;
	int width, height;

	sima->zoom= zoom;

	if (sima->zoom > 0.1f && sima->zoom < 4.0f)
		return;

	/* check zoom limits */
	ED_space_image_size(sima, &width, &height);

	width *= sima->zoom;
	height *= sima->zoom;

	if((width < 4) && (height < 4))
		sima->zoom= oldzoom;
	else if((ar->winrct.xmax - ar->winrct.xmin) <= sima->zoom)
		sima->zoom= oldzoom;
	else if((ar->winrct.ymax - ar->winrct.ymin) <= sima->zoom)
		sima->zoom= oldzoom;
}

static void sima_zoom_set_factor(SpaceImage *sima, ARegion *ar, float zoomfac)
{
	sima_zoom_set(sima, ar, sima->zoom*zoomfac);
}

int space_image_poll(bContext *C)
{
	SpaceLink *slink= CTX_wm_space_data(C);

	return (slink && (slink->spacetype == SPACE_IMAGE));
}

int space_image_main_area_poll(bContext *C)
{
	SpaceLink *slink= CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);

	if(slink && (slink->spacetype == SPACE_IMAGE))
		return (ar && ar->type->regionid == RGN_TYPE_WINDOW);
	
	return 0;
}

/********************** view pan operator *********************/

typedef struct ViewPanData {
	float x, y;
	float xof, yof;
} ViewPanData;

static void view_pan_init(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ViewPanData *vpd;

	op->customdata= vpd= MEM_callocN(sizeof(ViewPanData), "ImageViewPanData");
	WM_cursor_modal(CTX_wm_window(C), BC_NSEW_SCROLLCURSOR);

	vpd->x= event->x;
	vpd->y= event->y;
	vpd->xof= sima->xof;
	vpd->yof= sima->yof;

	WM_event_add_modal_handler(C, &CTX_wm_window(C)->handlers, op);
}

static void view_pan_exit(bContext *C, wmOperator *op, int cancel)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ViewPanData *vpd= op->customdata;

	if(cancel) {
		sima->xof= vpd->xof;
		sima->yof= vpd->yof;
		ED_area_tag_redraw(CTX_wm_area(C));
	}

	WM_cursor_restore(CTX_wm_window(C));
	MEM_freeN(op->customdata);
}

static int view_pan_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	float offset[2];

	RNA_float_get_array(op->ptr, "offset", offset);
	sima->xof += offset[0];
	sima->yof += offset[1];

	ED_area_tag_redraw(CTX_wm_area(C));

	/* XXX notifier? */
#if 0
	if(image_preview_active(curarea, NULL, NULL)) {
		/* recalculates new preview rect */
		scrarea_do_windraw(curarea);
		image_preview_event(2);
	}
#endif
	
	return OPERATOR_FINISHED;
}

static int view_pan_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	view_pan_init(C, op, event);
	return OPERATOR_RUNNING_MODAL;
}

static int view_pan_modal(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ViewPanData *vpd= op->customdata;
	float offset[2];

	switch(event->type) {
		case MOUSEMOVE:
			sima->xof= vpd->xof;
			sima->yof= vpd->yof;
			offset[0]= (vpd->x - event->x)/sima->zoom;
			offset[1]= (vpd->y - event->y)/sima->zoom;
			RNA_float_set_array(op->ptr, "offset", offset);
			view_pan_exec(C, op);
			break;
		case MIDDLEMOUSE:
			if(event->val==0) {
				view_pan_exit(C, op, 0);
				return OPERATOR_FINISHED;
			}
			break;
	}

	return OPERATOR_RUNNING_MODAL;
}

static int view_pan_cancel(bContext *C, wmOperator *op)
{
	view_pan_exit(C, op, 1);
	return OPERATOR_CANCELLED;
}

void IMAGE_OT_view_pan(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "View Pan";
	ot->idname= "IMAGE_OT_view_pan";
	
	/* api callbacks */
	ot->exec= view_pan_exec;
	ot->invoke= view_pan_invoke;
	ot->modal= view_pan_modal;
	ot->cancel= view_pan_cancel;
	ot->poll= space_image_main_area_poll;
	
	/* properties */
	RNA_def_float_vector(ot->srna, "offset", 2, NULL, -FLT_MAX, FLT_MAX,
		"Offset", "Offset in floating point units, 1.0 is the width and height of the image.", -FLT_MAX, FLT_MAX);
}

/********************** view zoom operator *********************/

typedef struct ViewZoomData {
	float x, y;
	float zoom;
} ViewZoomData;

static void view_zoom_init(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ViewZoomData *vpd;

	op->customdata= vpd= MEM_callocN(sizeof(ViewZoomData), "ImageViewZoomData");
	WM_cursor_modal(CTX_wm_window(C), BC_NSEW_SCROLLCURSOR);

	vpd->x= event->x;
	vpd->y= event->y;
	vpd->zoom= sima->zoom;

	WM_event_add_modal_handler(C, &CTX_wm_window(C)->handlers, op);
}

static void view_zoom_exit(bContext *C, wmOperator *op, int cancel)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ViewZoomData *vpd= op->customdata;

	if(cancel) {
		sima->zoom= vpd->zoom;
		ED_area_tag_redraw(CTX_wm_area(C));
	}

	WM_cursor_restore(CTX_wm_window(C));
	MEM_freeN(op->customdata);
}

static int view_zoom_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);

	sima_zoom_set_factor(sima, ar, RNA_float_get(op->ptr, "factor"));

	ED_area_tag_redraw(CTX_wm_area(C));

	/* XXX notifier? */
#if 0
	if(image_preview_active(curarea, NULL, NULL)) {
		/* recalculates new preview rect */
		scrarea_do_windraw(curarea);
		image_preview_event(2);
	}
#endif
	
	return OPERATOR_FINISHED;
}

static int view_zoom_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	view_zoom_init(C, op, event);
	return OPERATOR_RUNNING_MODAL;
}

static int view_zoom_modal(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);
	ViewZoomData *vpd= op->customdata;
	float factor;

	switch(event->type) {
		case MOUSEMOVE:
			factor= 1.0 + (vpd->x-event->x+vpd->y-event->y)/300.0f;
			RNA_float_set(op->ptr, "factor", factor);
			sima_zoom_set(sima, ar, vpd->zoom*factor);
			ED_area_tag_redraw(CTX_wm_area(C));
			break;
		case MIDDLEMOUSE:
			if(event->val==0) {
				view_zoom_exit(C, op, 0);
				return OPERATOR_FINISHED;
			}
			break;
	}

	return OPERATOR_RUNNING_MODAL;
}

static int view_zoom_cancel(bContext *C, wmOperator *op)
{
	view_zoom_exit(C, op, 1);
	return OPERATOR_CANCELLED;
}

void IMAGE_OT_view_zoom(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "View Zoom";
	ot->idname= "IMAGE_OT_view_zoom";
	
	/* api callbacks */
	ot->exec= view_zoom_exec;
	ot->invoke= view_zoom_invoke;
	ot->modal= view_zoom_modal;
	ot->cancel= view_zoom_cancel;
	ot->poll= space_image_main_area_poll;
	
	/* properties */
	RNA_def_float(ot->srna, "factor", 0.0f, 0.0f, FLT_MAX,
		"Factor", "Zoom factor, values higher than 1.0 zoom in, lower values zoom out.", -FLT_MAX, FLT_MAX);
}

/********************** view all operator *********************/

/* Updates the fields of the View2D member of the SpaceImage struct.
 * Default behavior is to reset the position of the image and set the zoom to 1
 * If the image will not fit within the window rectangle, the zoom is adjusted */

static int view_all_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima;
	ARegion *ar;
	Scene *scene;
	Object *obedit;
	Image *ima;
	ImBuf *ibuf;
	float aspx, aspy, zoomx, zoomy, w, h;
	int width, height;

	/* retrieve state */
	sima= (SpaceImage*)CTX_wm_space_data(C);
	ar= CTX_wm_region(C);
	scene= (Scene*)CTX_data_scene(C);
	obedit= CTX_data_edit_object(C);

	ima= ED_space_image(sima);
	ibuf= ED_space_image_buffer(sima);
	ED_space_image_size(sima, &width, &height);
	ED_space_image_aspect(sima, &aspx, &aspy);

	w= width*aspx;
	h= height*aspy;
	
	/* check if the image will fit in the image with zoom==1 */
	width = ar->winrct.xmax - ar->winrct.xmin + 1;
	height = ar->winrct.ymax - ar->winrct.ymin + 1;

	if((w >= width || h >= height) && (width > 0 && height > 0)) {
		/* find the zoom value that will fit the image in the image space */
		zoomx= width/w;
		zoomy= height/h;
		sima_zoom_set(sima, ar, 1.0f/power_of_2(1/MIN2(zoomx, zoomy)));
	}
	else
		sima_zoom_set(sima, ar, 1.0f);

	sima->xof= sima->yof= 0.0f;

	ED_area_tag_redraw(CTX_wm_area(C));
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_view_all(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "View All";
	ot->idname= "IMAGE_OT_view_all";
	
	/* api callbacks */
	ot->exec= view_all_exec;
	ot->poll= space_image_main_area_poll;
}

/********************** view selected operator *********************/

static int view_selected_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima;
	ARegion *ar;
	Scene *scene;
	Object *obedit;
	Image *ima;
	ImBuf *ibuf;
	float size, min[2], max[2], d[2];
	int width, height;

	/* retrieve state */
	sima= (SpaceImage*)CTX_wm_space_data(C);
	ar= CTX_wm_region(C);
	scene= (Scene*)CTX_data_scene(C);
	obedit= CTX_data_edit_object(C);

	ima= ED_space_image(sima);
	ibuf= ED_space_image_buffer(sima);
	ED_space_image_size(sima, &width, &height);

	/* get bounds */
	if(!ED_uvedit_minmax(scene, ima, obedit, min, max))
		return OPERATOR_CANCELLED;

	/* adjust offset and zoom */
	sima->xof= (int)(((min[0] + max[0])*0.5f - 0.5f)*width);
	sima->yof= (int)(((min[1] + max[1])*0.5f - 0.5f)*height);

	d[0]= max[0] - min[0];
	d[1]= max[1] - min[1];
	size= 0.5*MAX2(d[0], d[1])*MAX2(width, height)/256.0f;
	
	if(size<=0.01) size= 0.01;
	sima_zoom_set(sima, ar, 0.7/size);

	ED_area_tag_redraw(CTX_wm_area(C));
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_view_selected(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "View Center";
	ot->idname= "IMAGE_OT_view_selected";
	
	/* api callbacks */
	ot->exec= view_selected_exec;
	ot->poll= ED_operator_uvedit;
}

/********************** view zoom in/out operator *********************/

static int view_zoom_in_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);

	sima_zoom_set_factor(sima, ar, 1.25f);

	ED_area_tag_redraw(CTX_wm_area(C));
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_view_zoom_in(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "View Zoom In";
	ot->idname= "IMAGE_OT_view_zoom_in";
	
	/* api callbacks */
	ot->exec= view_zoom_in_exec;
	ot->poll= space_image_main_area_poll;
}

static int view_zoom_out_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);

	sima_zoom_set_factor(sima, ar, 0.8f);

	ED_area_tag_redraw(CTX_wm_area(C));
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_view_zoom_out(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "View Zoom Out";
	ot->idname= "IMAGE_OT_view_zoom_out";
	
	/* api callbacks */
	ot->exec= view_zoom_out_exec;
	ot->poll= space_image_main_area_poll;
}

/********************** view zoom ratio operator *********************/

static int view_zoom_ratio_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);

	sima_zoom_set(sima, ar, RNA_float_get(op->ptr, "ratio"));
	
	/* ensure pixel exact locations for draw */
	sima->xof= (int)sima->xof;
	sima->yof= (int)sima->yof;

	/* XXX notifier? */
#if 0
	if(image_preview_active(curarea, NULL, NULL)) {
		/* recalculates new preview rect */
		scrarea_do_windraw(curarea);
		image_preview_event(2);
	}
#endif

	ED_area_tag_redraw(CTX_wm_area(C));
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_view_zoom_ratio(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "View Zoom Ratio";
	ot->idname= "IMAGE_OT_view_zoom_ratio";
	
	/* api callbacks */
	ot->exec= view_zoom_ratio_exec;
	ot->poll= space_image_main_area_poll;
	
	/* properties */
	RNA_def_float(ot->srna, "ratio", 0.0f, 0.0f, FLT_MAX,
		"Ratio", "Zoom ratio, 1.0 is 1:1, higher is zoomed in, lower is zoomed out.", -FLT_MAX, FLT_MAX);
}

/**************** load/replace/save callbacks ******************/

static char *filesel_imagetype_string(Image *ima)
{
	char *strp, *str= MEM_callocN(14*32, "menu for filesel");
	
	strp= str;
	str += sprintf(str, "Save Image as: %%t|");
	str += sprintf(str, "Targa %%x%d|", R_TARGA);
	str += sprintf(str, "Targa Raw %%x%d|", R_RAWTGA);
	str += sprintf(str, "PNG %%x%d|", R_PNG);
	str += sprintf(str, "BMP %%x%d|", R_BMP);
	str += sprintf(str, "Jpeg %%x%d|", R_JPEG90);
	str += sprintf(str, "Iris %%x%d|", R_IRIS);
	if(G.have_libtiff)
		str += sprintf(str, "Tiff %%x%d|", R_TIFF);
	str += sprintf(str, "Radiance HDR %%x%d|", R_RADHDR);
	str += sprintf(str, "Cineon %%x%d|", R_CINEON);
	str += sprintf(str, "DPX %%x%d|", R_DPX);
#ifdef WITH_OPENEXR
	str += sprintf(str, "OpenEXR %%x%d|", R_OPENEXR);
	/* saving sequences of multilayer won't work, they copy buffers  */
	if(ima->source==IMA_SRC_SEQUENCE && ima->type==IMA_TYPE_MULTILAYER);
	else str += sprintf(str, "MultiLayer %%x%d|", R_MULTILAYER);
#endif	
	return strp;
}

static void image_filesel(bContext *C, wmOperator *op, const char *path)
{
	RNA_string_set(op->ptr, "filename", path);
	WM_event_add_fileselect(C, op); 
}

/******************** open image operator ********************/

static int open_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	Scene *scene= CTX_data_scene(C);
	Object *obedit= CTX_data_edit_object(C);
	Image *ima= NULL;
	char *str;

	str= RNA_string_get_alloc(op->ptr, "filename", NULL, 0);
	ima= BKE_add_image_file(str, scene->r.cfra);
	MEM_freeN(str);

	if(!ima)
		return OPERATOR_CANCELLED;

	BKE_image_signal(ima, &sima->iuser, IMA_SIGNAL_RELOAD);
	ED_space_image_set(C, sima, scene, obedit, ima);

	return OPERATOR_FINISHED;
}

static int open_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	char *path= (sima->image)? sima->image->name: U.textudir;

	if(RNA_property_is_set(op->ptr, "filename"))
		return open_exec(C, op);
	
	image_filesel(C, op, path);

	return OPERATOR_RUNNING_MODAL;
}

void IMAGE_OT_open(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Open";
	ot->idname= "IMAGE_OT_open";
	
	/* api callbacks */
	ot->exec= open_exec;
	ot->invoke= open_invoke;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* properties */
	RNA_def_string_file_path(ot->srna, "filename", "", 0, "Filename", "File path of image to open.");
}

/******************** replace image operator ********************/

static int replace_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	char *str;

	if(!sima->image)
		return OPERATOR_CANCELLED;
	
	str= RNA_string_get_alloc(op->ptr, "filename", NULL, 0);
	BLI_strncpy(sima->image->name, str, sizeof(sima->image->name)-1); /* we cant do much if the str is longer then 240 :/ */
	MEM_freeN(str);

	BKE_image_signal(sima->image, &sima->iuser, IMA_SIGNAL_RELOAD);
	WM_event_add_notifier(C, NC_IMAGE|NA_EDITED, sima->image);

	return OPERATOR_FINISHED;
}

static int replace_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	char *path= (sima->image)? sima->image->name: U.textudir;

	if(!sima->image)
		return OPERATOR_CANCELLED;

	if(RNA_property_is_set(op->ptr, "filename"))
		return replace_exec(C, op);

	image_filesel(C, op, path);

	return OPERATOR_RUNNING_MODAL;
}

void IMAGE_OT_replace(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Replace";
	ot->idname= "IMAGE_OT_replace";
	
	/* api callbacks */
	ot->exec= replace_exec;
	ot->invoke= replace_invoke;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* properties */
	RNA_def_string_file_path(ot->srna, "filename", "", 0, "Filename", "File path of image to replace current image with.");
}

/******************** save image as operator ********************/

static void save_image_doit(bContext *C, SpaceImage *sima, Scene *scene, wmOperator *op, char *name)
{
	Image *ima= ED_space_image(sima);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	int len;
	char str[FILE_MAXDIR+FILE_MAXFILE];

	if (ibuf) {
		BLI_strncpy(str, name, sizeof(str));

		BLI_convertstringcode(str, G.sce);
		BLI_convertstringframe(str, scene->r.cfra);
		
		
		if(scene->r.scemode & R_EXTENSION)  {
			BKE_add_image_extension(scene, str, sima->imtypenr);
			BKE_add_image_extension(scene, name, sima->imtypenr);
		}
		
		if(1) { // XXX saveover(str)) {
			
			/* enforce user setting for RGB or RGBA, but skip BW */
			if(scene->r.planes==32)
				ibuf->depth= 32;
			else if(scene->r.planes==24)
				ibuf->depth= 24;
			
			WM_cursor_wait(1);

			if(sima->imtypenr==R_MULTILAYER) {
				RenderResult *rr= BKE_image_get_renderresult(scene, ima);
				if(rr) {
					RE_WriteRenderResult(rr, str, scene->r.quality);
					
					BLI_strncpy(ima->name, name, sizeof(ima->name));
					BLI_strncpy(ibuf->name, str, sizeof(ibuf->name));
					
					/* should be function? nevertheless, saving only happens here */
					for(ibuf= ima->ibufs.first; ibuf; ibuf= ibuf->next)
						ibuf->userflags &= ~IB_BITMAPDIRTY;
					
				}
				else
					BKE_report(op->reports, RPT_ERROR, "Did not write, no Multilayer Image");
			}
			else if (BKE_write_ibuf(scene, ibuf, str, sima->imtypenr, scene->r.subimtype, scene->r.quality)) {
				BLI_strncpy(ima->name, name, sizeof(ima->name));
				BLI_strncpy(ibuf->name, str, sizeof(ibuf->name));
				
				ibuf->userflags &= ~IB_BITMAPDIRTY;
				
				/* change type? */
				if( ELEM(ima->source, IMA_SRC_GENERATED, IMA_SRC_VIEWER)) {
					ima->source= IMA_SRC_FILE;
					ima->type= IMA_TYPE_IMAGE;
				}
				if(ima->type==IMA_TYPE_R_RESULT)
					ima->type= IMA_TYPE_IMAGE;
				
				/* name image as how we saved it */
				len= strlen(str);
				while (len > 0 && str[len - 1] != '/' && str[len - 1] != '\\') len--;
				rename_id(&ima->id, str+len);
			} 
			else
				BKE_reportf(op->reports, RPT_ERROR, "Couldn't write image: %s", str);

			WM_event_add_notifier(C, NC_IMAGE|NA_EDITED, sima->image);

			WM_cursor_wait(0);
		}
	}
}

static int save_as_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	Scene *scene= CTX_data_scene(C);
	Image *ima = ED_space_image(sima);
	char *str;

	if(!ima)
		return OPERATOR_CANCELLED;

	str= RNA_string_get_alloc(op->ptr, "filename", NULL, 0);
	save_image_doit(C, sima, scene, op, str);
	MEM_freeN(str);

	return OPERATOR_FINISHED;
}

static int save_as_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	Image *ima = ED_space_image(sima);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	Scene *scene= CTX_data_scene(C);

	if(RNA_property_is_set(op->ptr, "filename"))
		return save_as_exec(C, op);
	
	if(!ima)
		return OPERATOR_CANCELLED;

	/* always opens fileselect */
	if(ibuf) {
		char *strp;
		
		strp= filesel_imagetype_string(ima);
		
		/* cant save multilayer sequence, ima->rr isn't valid for a specific frame */
		if(ima->rr && !(ima->source==IMA_SRC_SEQUENCE && ima->type==IMA_TYPE_MULTILAYER))
			sima->imtypenr= R_MULTILAYER;
		else if(ima->type==IMA_TYPE_R_RESULT)
			sima->imtypenr= scene->r.imtype;
		else
			sima->imtypenr= BKE_ftype_to_imtype(ibuf->ftype);
		
		// XXX activate_fileselect_menu(FILE_SPECIAL, "Save Image", name, strp, &sima->imtypenr, save_image_doit);
		// XXX note: we can give default menu enums to operator for this 
		
		image_filesel(C, op, ima->name);

		return OPERATOR_RUNNING_MODAL;
	}

	return OPERATOR_CANCELLED;
}

void IMAGE_OT_save_as(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Save As";
	ot->idname= "IMAGE_OT_save_as";
	
	/* api callbacks */
	ot->exec= save_as_exec;
	ot->invoke= save_as_invoke;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* properties */
	RNA_def_string_file_path(ot->srna, "filename", "", 0, "Filename", "File path to save image to.");
}

/******************** save image operator ********************/

static int save_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	Image *ima = ED_space_image(sima);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	Scene *scene= CTX_data_scene(C);
	char name[FILE_MAXDIR+FILE_MAXFILE];

	if(!ima)
		return OPERATOR_CANCELLED;

	/* if exists, saves over without fileselect */

	strcpy(name, ima->name);

	if(ibuf) {
		if(BLI_exists(ibuf->name)) {
			if(BKE_image_get_renderresult(scene, ima)) 
				sima->imtypenr= R_MULTILAYER;
			else 
				sima->imtypenr= BKE_ftype_to_imtype(ibuf->ftype);
			
			save_image_doit(C, sima, scene, op, ibuf->name);
		}
		else
			return save_as_exec(C, op);
	}
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_save(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Save";
	ot->idname= "IMAGE_OT_save";
	
	/* api callbacks */
	ot->exec= save_exec;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/******************* save sequence operator ********************/

static int save_sequence_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ImBuf *ibuf;
	int tot= 0;
	char di[FILE_MAX], fi[FILE_MAX];
	
	if(sima->image==NULL)
		return OPERATOR_CANCELLED;

	if(sima->image->source!=IMA_SRC_SEQUENCE) {
		BKE_report(op->reports, RPT_ERROR, "Can only save sequence on image sequences.");
		return OPERATOR_CANCELLED;
	}

	if(sima->image->type==IMA_TYPE_MULTILAYER) {
		BKE_report(op->reports, RPT_ERROR, "Can't save multilayer sequences.");
		return OPERATOR_CANCELLED;
	}
	
	/* get total */
	for(ibuf= sima->image->ibufs.first; ibuf; ibuf= ibuf->next) 
		if(ibuf->userflags & IB_BITMAPDIRTY)
			tot++;
	
	if(tot==0) {
		BKE_report(op->reports, RPT_WARNING, "No images have been changed.");
		return OPERATOR_CANCELLED;
	}

	/* get a filename for menu */
	for(ibuf= sima->image->ibufs.first; ibuf; ibuf= ibuf->next) 
		if(ibuf->userflags & IB_BITMAPDIRTY)
			break;
	
	BLI_strncpy(di, ibuf->name, FILE_MAX);
	BLI_splitdirstring(di, fi);
	
	BKE_reportf(op->reports, RPT_INFO, "%d Image(s) will be saved in %s", tot, di);

	for(ibuf= sima->image->ibufs.first; ibuf; ibuf= ibuf->next) {
		if(ibuf->userflags & IB_BITMAPDIRTY) {
			char name[FILE_MAX];
			BLI_strncpy(name, ibuf->name, sizeof(name));
			
			BLI_convertstringcode(name, G.sce);

			if(0 == IMB_saveiff(ibuf, name, IB_rect | IB_zbuf | IB_zbuffloat)) {
				BKE_reportf(op->reports, RPT_ERROR, "Could not write image %s.", name);
				break;
			}

			printf("Saved: %s\n", ibuf->name);
			ibuf->userflags &= ~IB_BITMAPDIRTY;
		}
	}

	return OPERATOR_FINISHED;
}

void IMAGE_OT_save_sequence(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Save Sequence";
	ot->idname= "IMAGE_OT_save_sequence";
	
	/* api callbacks */
	ot->exec= save_sequence_exec;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/******************** reload image operator ********************/

static int reload_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima;

	/* retrieve state */
	sima= (SpaceImage*)CTX_wm_space_data(C);

	if(!sima->image)
		return OPERATOR_CANCELLED;

	BKE_image_signal(sima->image, &sima->iuser, IMA_SIGNAL_RELOAD);
	/* ED_space_image_set(C, sima, scene, obedit, NULL); - do we really need this? */

	// XXX BIF_preview_changed(ID_TE);
	WM_event_add_notifier(C, NC_IMAGE|NA_EDITED, sima->image);
	ED_area_tag_redraw(CTX_wm_area(C));
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_reload(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Reload";
	ot->idname= "IMAGE_OT_reload";
	
	/* api callbacks */
	ot->exec= reload_exec;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/********************** new image operator *********************/

static int new_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima;
	Scene *scene;
	Object *obedit;
	Image *ima;
	char name[22];
	float color[4];
	int width, height, floatbuf, uvtestgrid;

	/* retrieve state */
	sima= (SpaceImage*)CTX_wm_space_data(C);
	scene= (Scene*)CTX_data_scene(C);
	obedit= CTX_data_edit_object(C);

	RNA_string_get(op->ptr, "name", name);
	width= RNA_int_get(op->ptr, "width");
	height= RNA_int_get(op->ptr, "height");
	floatbuf= RNA_boolean_get(op->ptr, "float");
	uvtestgrid= RNA_boolean_get(op->ptr, "uv_test_grid");
	RNA_float_get_array(op->ptr, "color", color);
	color[3]= RNA_float_get(op->ptr, "alpha");

	ima = BKE_add_image_size(width, height, name, floatbuf, uvtestgrid, color);
	BKE_image_signal(sima->image, &sima->iuser, IMA_SIGNAL_USER_NEW_IMAGE);
	ED_space_image_set(C, sima, scene, obedit, ima);
	
	return OPERATOR_FINISHED;
}

void IMAGE_OT_new(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "New";
	ot->idname= "IMAGE_OT_new";
	
	/* api callbacks */
	ot->exec= new_exec;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* properties */
	RNA_def_string(ot->srna, "name", "Untitled", 21, "Name", "Image datablock name.");
	RNA_def_int(ot->srna, "width", 1024, 1, INT_MAX, "Width", "Image width.", 1, 16384);
	RNA_def_int(ot->srna, "height", 1024, 1, INT_MAX, "Height", "Image height.", 1, 16384);
	RNA_def_float_color(ot->srna, "color", 3, NULL, 0.0f, FLT_MAX, "Color", "Default fill color.", 0.0f, 1.0f);
	RNA_def_float(ot->srna, "alpha", 1.0f, 0.0f, 1.0f, "Alpha", "Default fill alpha.", 0.0f, 1.0f);
	RNA_def_boolean(ot->srna, "uv_test_grid", 0, "UV Test Grid", "Fill the image with a grid for UV map testing.");
	RNA_def_boolean(ot->srna, "float", 0, "32 bit Float", "Create image with 32 bit floating point bit depth.");
}

/********************* pack operator *********************/

static int pack_test(bContext *C, wmOperator *op)
{
	Image *ima= CTX_data_edit_image(C);
	int as_png= RNA_boolean_get(op->ptr, "as_png");

	if(!ima)
		return 0;
	if(!as_png && ima->packedfile)
		return 0;

	if(ima->source==IMA_SRC_SEQUENCE || ima->source==IMA_SRC_MOVIE) {
		BKE_report(op->reports, RPT_ERROR, "Can't pack movie or image sequence.");
		return 0;
	}

	return 1;
}

static int pack_exec(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	Image *ima= ED_space_image(sima);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	int as_png= RNA_boolean_get(op->ptr, "as_png");

	if(!pack_test(C, op))
		return OPERATOR_CANCELLED;
	
	if(!as_png && (ibuf && (ibuf->userflags & IB_BITMAPDIRTY))) {
		BKE_report(op->reports, RPT_ERROR, "Can't pack edited image from disk, only as internal PNG.");
		return OPERATOR_CANCELLED;
	}

	if(as_png)
		BKE_image_memorypack(ima);
	else
		ima->packedfile= newPackedFile(ima->name);

	return OPERATOR_FINISHED;
}

static int pack_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	uiMenuItem *head;
	int as_png= RNA_boolean_get(op->ptr, "as_png");

	if(!pack_test(C, op))
		return OPERATOR_CANCELLED;
	
	if(!as_png && (ibuf && (ibuf->userflags & IB_BITMAPDIRTY))) {
		head= uiPupMenuBegin("OK", ICON_HELP);
		uiMenuItemBooleanO(head, "Can't pack edited image from disk. Pack as internal PNG?", 0, op->idname, "as_png", 1);
		uiPupMenuEnd(C, head);

		return OPERATOR_CANCELLED;
	}

	return pack_exec(C, op);
}

void IMAGE_OT_pack(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Pack";
	ot->idname= "IMAGE_OT_pack";
	
	/* api callbacks */
	ot->exec= pack_exec;
	ot->invoke= pack_invoke;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;

	/* properties */
	RNA_def_boolean(ot->srna, "as_png", 0, "Pack As PNG", "Pack image as lossless PNG.");
}

/********************* unpack operator *********************/

static int unpack_exec(bContext *C, wmOperator *op)
{
	Image *ima= CTX_data_edit_image(C);

	if(!ima)
		return OPERATOR_CANCELLED;
	if(!ima->packedfile)
		return OPERATOR_CANCELLED;

	if(ima->source==IMA_SRC_SEQUENCE || ima->source==IMA_SRC_MOVIE) {
		BKE_report(op->reports, RPT_ERROR, "Can't unpack movie or image sequence.");
		return OPERATOR_CANCELLED;
	}

	if(G.fileflags & G_AUTOPACK)
		BKE_report(op->reports, RPT_WARNING, "AutoPack is enabled, so image will be packed again on file save.");
	
	unpackImage(ima, PF_ASK);

	return OPERATOR_FINISHED;
}

void IMAGE_OT_unpack(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Unpack";
	ot->idname= "IMAGE_OT_unpack";
	
	/* api callbacks */
	ot->exec= unpack_exec;
	ot->poll= space_image_poll;

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
}

/******************** sample image operator ********************/

typedef struct ImageSampleInfo {
	ARegionType *art;
	void *draw_handle;
	int x, y;

	char col[4];
	float colf[4];
	int z;
	float zf;

	char *colp;
	float *colfp;
	int *zp;
	float *zfp;

	int draw;
} ImageSampleInfo;

static void sample_draw(const bContext *C, ARegion *ar, void *arg_info)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	ImageSampleInfo *info= arg_info;

	if(ibuf == NULL)
		return;
	
	draw_image_info(ar, ibuf->channels, info->x, info->y, info->colp,
		info->colfp, info->zp, info->zfp);
}

static void sample_apply(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	ImageSampleInfo *info= op->customdata;
	float fx, fy;
	int x, y;
	
	if(ibuf == NULL)
		return;

	x= event->x - ar->winrct.xmin;
	y= event->y - ar->winrct.ymin;
	UI_view2d_region_to_view(&ar->v2d, x, y, &fx, &fy);

	if(fx>=0.0 && fy>=0.0 && fx<1.0 && fy<1.0) {
		float *fp;
		char *cp;
		int x= (int)(fx*ibuf->x), y= (int)(fy*ibuf->y);

		CLAMP(x, 0, ibuf->x-1);
		CLAMP(y, 0, ibuf->y-1);

		info->x= x;
		info->y= y;
		info->draw= 1;

		info->colp= NULL;
		info->colfp= NULL;
		info->zp= NULL;
		info->zfp= NULL;
		
		if(ibuf->rect) {
			cp= (char *)(ibuf->rect + y*ibuf->x + x);

			info->col[0]= cp[0];
			info->col[1]= cp[1];
			info->col[2]= cp[2];
			info->col[3]= cp[3];
			info->colp= info->col;

			info->colf[0]= (float)cp[0]/255.0f;
			info->colf[1]= (float)cp[1]/255.0f;
			info->colf[2]= (float)cp[2]/255.0f;
			info->colf[3]= (float)cp[3]/255.0f;
			info->colfp= info->colf;
		}
		if(ibuf->rect_float) {
			fp= (ibuf->rect_float + (ibuf->channels)*(y*ibuf->x + x));

			info->colf[0]= fp[0];
			info->colf[1]= fp[1];
			info->colf[2]= fp[2];
			info->colf[3]= fp[4];
			info->colfp= info->colf;
		}

		if(ibuf->zbuf) {
			info->z= ibuf->zbuf[y*ibuf->x + x];
			info->zp= &info->z;
		}
		if(ibuf->zbuf_float) {
			info->zf= ibuf->zbuf_float[y*ibuf->x + x];
			info->zfp= &info->zf;
		}
		
		if(sima->cumap && ibuf->channels==4) {
			/* we reuse this callback for set curves point operators */
			if(RNA_struct_find_property(op->ptr, "point")) {
				int point= RNA_enum_get(op->ptr, "point");

				if(point == 1) {
					curvemapping_set_black_white(sima->cumap, NULL, info->colfp);
					curvemapping_do_ibuf(sima->cumap, ibuf);
				}
				else if(point == 0) {
					curvemapping_set_black_white(sima->cumap, info->colfp, NULL);
					curvemapping_do_ibuf(sima->cumap, ibuf);
				}
			}
		}
				
		// XXX node curve integration ..
#if 0
		{
			ScrArea *sa, *cur= curarea;
			
			node_curvemap_sample(fp);	/* sends global to node editor */
			for(sa= G.curscreen->areabase.first; sa; sa= sa->next) {
				if(sa->spacetype==SPACE_NODE) {
					areawinset(sa->win);
					scrarea_do_windraw(sa);
				}
			}
			node_curvemap_sample(NULL);		/* clears global in node editor */
			curarea= cur;
		}
#endif
	}
	else
		info->draw= 0;

	ED_area_tag_redraw(CTX_wm_area(C));
}

static void sample_exit(bContext *C, wmOperator *op)
{
	ImageSampleInfo *info= op->customdata;

	ED_region_draw_cb_exit(info->art, info->draw_handle);
	ED_area_tag_redraw(CTX_wm_area(C));
	MEM_freeN(info);
}

static int sample_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	ARegion *ar= CTX_wm_region(C);
	ImBuf *ibuf= ED_space_image_buffer(sima);
	ImageSampleInfo *info;

	if(ibuf == NULL)
		return OPERATOR_CANCELLED;
	
	info= MEM_callocN(sizeof(ImageSampleInfo), "ImageSampleInfo");
	info->art= ar->type;
	info->draw_handle = ED_region_draw_cb_activate(ar->type, sample_draw, info, REGION_DRAW_POST);
	op->customdata= info;

	sample_apply(C, op, event);

	WM_event_add_modal_handler(C, &CTX_wm_window(C)->handlers, op);

	return OPERATOR_RUNNING_MODAL;
}

static int sample_modal(bContext *C, wmOperator *op, wmEvent *event)
{
	switch(event->type) {
		case LEFTMOUSE:
		case RIGHTMOUSE: // XXX hardcoded
			sample_exit(C, op);
			return OPERATOR_CANCELLED;
		case MOUSEMOVE:
			sample_apply(C, op, event);
			break;
	}

	return OPERATOR_RUNNING_MODAL;
}

static int sample_cancel(bContext *C, wmOperator *op)
{
	sample_exit(C, op);
	return OPERATOR_CANCELLED;
}

void IMAGE_OT_sample(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Sample";
	ot->idname= "IMAGE_OT_sample";
	
	/* api callbacks */
	ot->invoke= sample_invoke;
	ot->modal= sample_modal;
	ot->cancel= sample_cancel;
	ot->poll= space_image_main_area_poll;
}

/******************** set curve point operator ********************/

void IMAGE_OT_set_curves_point(wmOperatorType *ot)
{
	static EnumPropertyItem point_items[]= {
		{0, "BLACK_POINT", "Black Point", ""},
		{1, "WHITE_POINT", "White Point", ""},
		{0, NULL, NULL, NULL}};

	/* identifiers */
	ot->name= "Set Curves Point";
	ot->idname= "IMAGE_OT_set_curves_point";

	/* flags */
	ot->flag= OPTYPE_REGISTER|OPTYPE_UNDO;
	
	/* api callbacks */
	ot->invoke= sample_invoke;
	ot->modal= sample_modal;
	ot->cancel= sample_cancel;
	ot->poll= space_image_main_area_poll;

	/* properties */
	RNA_def_enum(ot->srna, "point", point_items, 0, "Point", "Set black point or white point for curves.");
}

/******************** record composite operator *********************/

typedef struct RecordCompositeData {
	wmTimer *timer;
	int old_cfra;
	int sfra, efra;
} RecordCompositeData;

int record_composite_apply(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	RecordCompositeData *rcd= op->customdata;
	Scene *scene= CTX_data_scene(C);
	ImBuf *ibuf;
	
	WM_timecursor(CTX_wm_window(C), scene->r.cfra);

	// XXX scene->nodetree->test_break= blender_test_break;
	// XXX scene->nodetree->test_break= NULL;
	
	BKE_image_all_free_anim_ibufs(scene->r.cfra);
	ntreeCompositTagAnimated(scene->nodetree);
	ntreeCompositExecTree(scene->nodetree, &scene->r, scene->r.cfra != rcd->old_cfra);	/* 1 is no previews */

	ED_area_tag_redraw(CTX_wm_area(C));
	
	ibuf= BKE_image_get_ibuf(sima->image, &sima->iuser);
	/* save memory in flipbooks */
	if(ibuf)
		imb_freerectfloatImBuf(ibuf);
	
	scene->r.cfra++;

	return (scene->r.cfra <= rcd->efra);
}

static int record_composite_init(bContext *C, wmOperator *op)
{
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	Scene *scene= CTX_data_scene(C);
	RecordCompositeData *rcd;

	if(sima->iuser.frames < 2)
		return 0;
	if(scene->nodetree == NULL)
		return 0;
	
	op->customdata= rcd= MEM_callocN(sizeof(RecordCompositeData), "ImageRecordCompositeData");

	rcd->old_cfra= scene->r.cfra;
	rcd->sfra= sima->iuser.sfra;
	rcd->efra= sima->iuser.sfra + sima->iuser.frames-1;
	scene->r.cfra= rcd->sfra;

	return 1;
}

static void record_composite_exit(bContext *C, wmOperator *op)
{
	Scene *scene= CTX_data_scene(C);
	SpaceImage *sima= (SpaceImage*)CTX_wm_space_data(C);
	RecordCompositeData *rcd= op->customdata;

	scene->r.cfra= rcd->old_cfra;

	WM_cursor_restore(CTX_wm_window(C));

	if(rcd->timer)
		WM_event_remove_window_timer(CTX_wm_window(C), rcd->timer);

	WM_event_add_notifier(C, NC_IMAGE|NA_EDITED, sima->image);

	// XXX play_anim(0);
	// XXX allqueue(REDRAWNODE, 1);

	MEM_freeN(rcd);
}

static int record_composite_exec(bContext *C, wmOperator *op)
{
	if(!record_composite_init(C, op))
		return OPERATOR_CANCELLED;
	
	while(record_composite_apply(C, op))
		;
	
	record_composite_exit(C, op);
	
	return OPERATOR_FINISHED;
}

static int record_composite_invoke(bContext *C, wmOperator *op, wmEvent *event)
{
	RecordCompositeData *rcd= op->customdata;
	
	if(!record_composite_init(C, op))
		return OPERATOR_CANCELLED;

	rcd= op->customdata;
	rcd->timer= WM_event_add_window_timer(CTX_wm_window(C), TIMER, 0.0f);
	WM_event_add_modal_handler(C, &CTX_wm_window(C)->handlers, op);

	if(!record_composite_apply(C, op))
		return OPERATOR_FINISHED;

	return OPERATOR_RUNNING_MODAL;
}

static int record_composite_modal(bContext *C, wmOperator *op, wmEvent *event)
{
	RecordCompositeData *rcd= op->customdata;

	switch(event->type) {
		case TIMER:
			if(rcd->timer == event->customdata) {
				if(!record_composite_apply(C, op)) {
					record_composite_exit(C, op);
					return OPERATOR_FINISHED;
				}
			}
			break;
		case ESCKEY:
			record_composite_exit(C, op);
			return OPERATOR_FINISHED;
	}

	return OPERATOR_RUNNING_MODAL;
}

static int record_composite_cancel(bContext *C, wmOperator *op)
{
	record_composite_exit(C, op);
	return OPERATOR_CANCELLED;
}

void IMAGE_OT_record_composite(wmOperatorType *ot)
{
	/* identifiers */
	ot->name= "Record Composite";
	ot->idname= "IMAGE_OT_record_composite";
	
	/* api callbacks */
	ot->exec= record_composite_exec;
	ot->invoke= record_composite_invoke;
	ot->modal= record_composite_modal;
	ot->cancel= record_composite_cancel;
	ot->poll= space_image_poll;
}

/******************** TODO ********************/

/* XXX notifier? */
#if 0
/* goes over all ImageUsers, and sets frame numbers if auto-refresh is set */
void BIF_image_update_frame(void)
{
	Tex *tex;
	
	/* texture users */
	for(tex= G.main->tex.first; tex; tex= tex->id.next) {
		if(tex->type==TEX_IMAGE && tex->ima)
			if(ELEM(tex->ima->source, IMA_SRC_MOVIE, IMA_SRC_SEQUENCE))
				if(tex->iuser.flag & IMA_ANIM_ALWAYS)
					BKE_image_user_calc_imanr(&tex->iuser, scene->r.cfra, 0);
		
	}
	/* image window, compo node users */
	if(G.curscreen) {
		ScrArea *sa;
		for(sa= G.curscreen->areabase.first; sa; sa= sa->next) {
			if(sa->spacetype==SPACE_VIEW3D) {
				View3D *v3d= sa->spacedata.first;
				if(v3d->bgpic)
					if(v3d->bgpic->iuser.flag & IMA_ANIM_ALWAYS)
						BKE_image_user_calc_imanr(&v3d->bgpic->iuser, scene->r.cfra, 0);
			}
			else if(sa->spacetype==SPACE_IMAGE) {
				SpaceImage *sima= sa->spacedata.first;
				if(sima->iuser.flag & IMA_ANIM_ALWAYS)
					BKE_image_user_calc_imanr(&sima->iuser, scene->r.cfra, 0);
			}
			else if(sa->spacetype==SPACE_NODE) {
				SpaceNode *snode= sa->spacedata.first;
				if((snode->treetype==NTREE_COMPOSIT) && (snode->nodetree)) {
					bNode *node;
					for(node= snode->nodetree->nodes.first; node; node= node->next) {
						if(node->id && node->type==CMP_NODE_IMAGE) {
							Image *ima= (Image *)node->id;
							ImageUser *iuser= node->storage;
							if(ELEM(ima->source, IMA_SRC_MOVIE, IMA_SRC_SEQUENCE))
								if(iuser->flag & IMA_ANIM_ALWAYS)
									BKE_image_user_calc_imanr(iuser, scene->r.cfra, 0);
						}
					}
				}
			}
		}
	}
}
#endif

