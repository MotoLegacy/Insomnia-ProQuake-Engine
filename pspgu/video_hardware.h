/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2007 Peter Mackay and Chris Swindle.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <psptypes.h>

void GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);

void GL_Upload8(int texture_index, const byte *data, int width, int height);
void GL_Upload16(int texture_index, const byte *data, int width, int height);
void GL_Upload24(int texture_index, const byte *data, int width, int height);
void GL_Upload32(int texture_index, const byte *data, int width, int height);

// Load a texture using the global (Quake) palette
int  GL_LoadTexture(const char *identifier, int width, int height, const byte *data, qboolean stretch_to_power_of_two, int filter, int mipmap_level);
// Loads a lightmap texture
int  GL_LoadLightmapTexture (const char *identifier, int width, int height, const byte *data,int bpp, int filter, qboolean update);
// Loads a texture with own palette (Half-Life, pcx, etc.)
int  GL_LoadPalettedTexture (const char *identifier, int width, int height, const byte *data, qboolean stretch_to_power_of_two, int filter, int mipmap_level, unsigned char* palette);
void GL_UnloadTexture (const int texture_index);

extern	int glx, gly, glwidth, glheight;

// r_local.h -- private refresh defs

#define ALIAS_BASE_SIZE_RATIO		(1.0 / 11.0)
					// normalizing factor so player model works out to about
					//  1 pixel per triangle
#define	MAX_LBM_HEIGHT		480

#define TILE_SIZE		128		// size of textures generated by R_GenTiledSurf

#define SKYSHIFT		7
#define	SKYSIZE			(1 << SKYSHIFT)
#define SKYMASK			(SKYSIZE - 1)

#define BACKFACE_EPSILON	0.01

void R_TimeRefresh_f (void);
void R_ReadPointFile_f (void);
texture_t *R_TextureAnimation (texture_t *base);

typedef struct surfcache_s
{
	struct surfcache_s	*next;
	struct surfcache_s 	**owner;		// NULL is an empty chunk of memory
	int					lightadj[MAXLIGHTMAPS]; // checked for strobe flush
	int					dlight;
	int					size;		// including header
	unsigned			width;
	unsigned			height;		// DEBUG only needed for debug
	float				mipscale;
	struct texture_s	*texture;	// checked for animating textures
	byte				data[4];	// width*height elements
} surfcache_t;


typedef struct
{
	pixel_t		*surfdat;	// destination for generated surface
	int			rowbytes;	// destination logical width in bytes
	msurface_t	*surf;		// description for surface to generate
	fixed8_t	lightadj[MAXLIGHTMAPS];
							// adjust for lightmap levels for dynamic lighting
	texture_t	*texture;	// corrected for animating textures
	int			surfmip;	// mipmapped ratio of surface texels / world pixels
	int			surfwidth;	// in mipmapped texels
	int			surfheight;	// in mipmapped texels
} drawsurf_t;


typedef enum {
	pt_static, pt_grav, pt_slowgrav, pt_fire, pt_explode, pt_explode2, pt_blob, pt_blob2
} ptype_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s
{
// driver-usable fields
	vec3_t		org;
	float		color;
// drivers never touch the following fields
	struct particle_s	*next;
	vec3_t		vel;
	float		ramp;
	float		die;
	ptype_t		type;
} particle_t;

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

typedef struct
{
	int			index;	// index into gltextures[].
} glpic_t;

//====================================================


extern	entity_t	r_worldentity;
extern	qboolean	r_cache_thrash;		// compatability
extern	vec3_t		modelorg, r_entorigin;
extern	entity_t	*currententity;
extern	int			r_visframecount;	// ??? what difs?
extern	int			r_framecount;
extern	mplane_t	frustum[4];
extern	int		c_brush_polys, c_alias_polys;


//
// view origin
//
extern	vec3_t	vup;
extern	vec3_t	vpn;
extern	vec3_t	vright;
extern	vec3_t	r_origin;

//
// screen size info
//
extern	refdef_t	r_refdef;
extern	mleaf_t		*r_viewleaf, *r_oldviewleaf;
extern	texture_t	*r_notexture_mip;
extern	int		d_lightstylevalue[256];	// 8.8 fraction of base light value

extern  int	    skyimage[6]; // Where sky images are stored
extern  int 	lightmap_index[64]; // Where lightmaps are stored

extern	qboolean	envmap;
extern	int	currenttexture;
extern	int	cnttextures[2];
extern	int	particletexture;
extern	int	playertextures;

extern	int	skytexturenum;		// index in cl.loadmodel, not gl texture object

extern	cvar_t	r_norefresh;
extern	cvar_t	r_drawentities;
extern	cvar_t	r_drawworld;
extern	cvar_t	r_drawviewmodel;
extern	cvar_t	r_speeds;
extern	cvar_t	r_waterwarp;
extern	cvar_t	r_fullbright;
extern	cvar_t	r_lightmap;
extern	cvar_t	r_shadows;
extern	cvar_t	r_mirroralpha;
extern	cvar_t	r_glassalpha;
extern	cvar_t	r_wateralpha;
extern	cvar_t	r_dynamic;
extern	cvar_t	r_novis;
extern	cvar_t	r_tex_scale_down;
extern	cvar_t	r_particles_simple;
extern	cvar_t	gl_keeptjunctions;
extern  cvar_t  r_interpolate_animation;
extern  cvar_t  r_interpolate_transform;
extern  cvar_t  r_model_contrast;
extern  cvar_t  r_model_brightness;
extern	cvar_t	fog;
extern	cvar_t	r_vsync;
extern  cvar_t	r_dithering;
extern	cvar_t	r_antialias;
extern	cvar_t	r_mipmaps;
extern	cvar_t	r_mipmaps_func;
extern	cvar_t	r_mipmaps_bias;
extern  cvar_t	r_skyclip;
extern  cvar_t  r_menufade;
extern  cvar_t  r_skyfog;
extern  cvar_t	r_showtris;
extern  cvar_t	r_test;

extern  cvar_t  vlight;   // RIOT - Vertex lighting
extern  cvar_t	vlight_pitch;
extern  cvar_t	vlight_yaw;
extern  cvar_t	vlight_lowcut;
extern  cvar_t	vlight_highcut;

extern	int			mirrortexturenum;	// quake texturenum, not gltexturenum
extern	qboolean	mirror;
extern	mplane_t	*mirror_plane;

extern	ScePspFMatrix4	r_world_matrix;

void R_TranslatePlayerSkin (int playernum);
void GL_Bind (int texture_index);
void GL_BindLightmap (int texture_index);

// Added by PM
int R_LightPoint (vec3_t p);
void R_DrawBrushModel (entity_t *e);
void R_AnimateLight (void);
void R_DrawWorld (void);
void R_RenderDlights (void);
void R_DrawParticles (void);
void R_DrawWaterSurfaces (void);
void R_RenderBrushPoly (msurface_t *fa);
void R_InitParticles (void);
void R_ClearParticles (void);
void R_Fog_f (void);

void Sky_LoadSkyBox (char *name);
void Sky_NewMap (void);
void Sky_Init (void);

void R_DrawSkyBox (void);
void R_ClearSkyBox (void);

void Fog_ParseServerMessage (void);
void Fog_SetupFrame (void);
void Fog_EnableGFog (void);
void Fog_DisableGFog (void);
void Fog_NewMap (void);
void Fog_Init (void);

void GL_BuildLightmaps (void);
void GL_MakeAliasModelDisplayLists (model_t *m, aliashdr_t *hdr);
void GL_Set2D (void);
void GL_SubdivideSurface (msurface_t *fa);
void GL_Surface (msurface_t *fa);

void EmitWaterPolys (msurface_t *fa);
void EmitSkyPolys (msurface_t *fa);
void EmitReflectivePolys (msurface_t *fa);
void EmitBothSkyLayers (msurface_t *fa);
void R_DrawSkyChain (msurface_t *s);
qboolean R_CullBox (vec3_t mins, vec3_t maxs);
void R_MarkLights (dlight_t *light, int bit, mnode_t *node);

//extern	float	bubblecolor[NUM_DLIGHTTYPES][4];

void R_RotateForEntity (entity_t *e);
void R_StoreEfrags (efrag_t **ppefrag);
void D_StartParticles (void);
// void D_DrawParticle (particle_t *pparticle);
void D_DrawParticle (particle_t *pparticle, vec3_t up, vec3_t right, float scale);
void D_EndParticles (void);

typedef struct {
	float s, t;
	unsigned int color;
	float x, y, z;
} part_vertex;

typedef struct {
	part_vertex first, second;
} psp_particle;


psp_particle* D_CreateBuffer (int size);
void 	  	  D_DeleteBuffer (psp_particle* vertices);
int 	      D_DrawParticleBuffered (psp_particle* vertices, particle_t *pparticle, vec3_t up, vec3_t right, float scale);
