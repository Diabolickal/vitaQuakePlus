/*
Copyright (C) 1996-1997 Id Software, Inc.

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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"
#define	SKY_TEX 2000

extern	model_t	*loadmodel;

//int		skytexturenum;

int		solidskytexture;
int		alphaskytexture;
float	speedscale;		// for top sky and bottom sky

//Diabolickal Skybox Support Start
int	    skytexorder[6] = {0,2,1,3,4,5};
int	    skyimage[6]; // Where sky images are stored
char	skybox_name[32] = ""; //name of current skybox, or "" if no skybox
//Diabolickal Skybox Support End

msurface_t	*warpface;

extern cvar_t gl_subdivide_size;

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;

	if (numverts > 60)
		Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m/gl_subdivide_size.value + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
	}
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;
	texture_t	*t;

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

//=========================================================



// speed up sin calculations - Ed
float	turbsin[] =
{
	#include "gl_warp_sin.h"
};
#define TURBSCALE (256.0 / (2 * M_PI))

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void EmitWaterPolys (msurface_t *fa)
{
	glpoly_t	*p;
	float		*v;
	int			i;
	float		s, t, os, ot;


	for (p=fa->polys ; p ; p=p->next)
	{
		float* pUV = gTexCoordBuffer;
		float* pPoint = gVertexBuffer;
		for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
		{
			os = v[3];
			ot = v[4];
			s = os + turbsin[(int)((ot*0.125+realtime) * TURBSCALE) & 255];
			s *= (0.015625f);
			t = ot + turbsin[(int)((os*0.125+realtime) * TURBSCALE) & 255];
			t *= (0.015625f);
			*pUV++ = s;
			*pUV++ = t;
			memcpy(pPoint, &v[0], sizeof(vec3_t));
			pPoint += 3;
		}
		vglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, p->numverts, gVertexBuffer);
		vglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, p->numverts, gTexCoordBuffer);
		GL_DrawPolygon(GL_TRIANGLE_FAN, p->numverts);
	}
}




/*
=============
EmitSkyPolys
=============
*/
void EmitSkyPolys (msurface_t *fa)
{
	glpoly_t	*p;
	float		*v;
	int			i;
	float	s, t;
	vec3_t	dir;
	float	length;

	for (p=fa->polys ; p ; p=p->next)
	{
		float* pUV = gTexCoordBuffer;
		float* pPoint = gVertexBuffer;
		for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
		{
			VectorSubtract (v, r_origin, dir);
			dir[2] *= 3;    // flatten the sphere
			length = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
			length = sqrt (length);
			length = 378/length;
			dir[0] *= length;
			dir[1] *= length;
			s = (speedscale + dir[0]) * (0.0078125f);
			t = (speedscale + dir[1]) * (0.0078125f);
			*pUV++ = s;
			*pUV++ = t;
			memcpy(pPoint, &v[0], sizeof(vec3_t));
			pPoint += 3;
		}
		vglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, p->numverts, gVertexBuffer);
		vglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, p->numverts, gTexCoordBuffer);
		GL_DrawPolygon(GL_TRIANGLE_FAN, p->numverts);
	}
}

/*
===============
EmitBothSkyLayers

Does a sky warp on the pre-fragmented glpoly_t chain
This will be called for brushmodels, the world
will have them chained together.
===============
*/
void EmitBothSkyLayers (msurface_t *fa)
{
	int			i;
	int			lindex;
	float		*vec;

	GL_Bind (solidskytexture);
	speedscale = realtime*8;
	speedscale -= (int)speedscale & ~127 ;

	EmitSkyPolys (fa);

	glEnable (GL_BLEND);
	GL_Bind (alphaskytexture);
	speedscale = realtime*16;
	speedscale -= (int)speedscale & ~127 ;

	EmitSkyPolys (fa);

	glDisable (GL_BLEND);
}
int	c_sky;
//Diabolickal Skybox Support Start
/*
#ifndef QUAKE2
=================
R_DrawSkyChain
=================
*/
/*
void R_DrawSkyChain (msurface_t *s)
{
	msurface_t	*fa;

	// used when gl_texsort is on
	GL_Bind(solidskytexture);
	speedscale = realtime*8;
	speedscale -= (int)speedscale & ~127 ;

	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa);

	glEnable (GL_BLEND);
	GL_Bind (alphaskytexture);
	speedscale = realtime*16;
	speedscale -= (int)speedscale & ~127 ;

	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa);

	glDisable (GL_BLEND);
}

#endif
*/
#define	MAX_CLIP_VERTS	64
static float s_axis;
static float t_axis;
static vec3_t v_axis;
float	skymins[2][6], skymaxs[2][6];
vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1}
};
int	st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},

	{1,3,2},
	{-1,-3,2},

	{-2,-1,3},		// 0 degrees yaw, look straight up
	{2,-1,-3}		// look straight down

//	{-1,2,3},
//	{1,2,-3}
};

// s = [0]/[2], t = [1]/[2]
int	vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},

	{1,3,2},
	{-1,3,-2},

	{-2,-1,3},
	{-2,1,-3}

//	{-1,2,3},
//	{1,2,-3}
};
void MakeSkyVec (float s, float t, int axis)
{
	vec3_t		b;
	int			j, k;

	b[0] = s*2000;
	b[1] = t*2000;
	b[2] = 2000;

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v_axis[j] = -b[-k - 1];
		else
			v_axis[j] = b[k - 1];
		v_axis[j] += r_origin[j];
	}

	// avoid bilerp seam
	s = (s+1.0f)*0.5f;
	t = (t+1.0f)*0.5f;

	if (s < 1.0f/512.0f)
		s = 1.0f/512.0f;
	else if (s > 511.0f/512.0f)
		s = 511.0f/512.0f;

	if (t < 1.0f/512.0f)
		t = 1.0f/512.0f;
	else if (t > 511.0f/512.0f)
		t = 511.0f/512.0f;

	t = 1.0f - t;

	s_axis = s;
	t_axis = t;
}
void DrawSkyPolygon (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	c_sky++;

	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];

		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}
void ClipSkyPolygon (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		Sys_Error ("ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		DrawSkyPolygon (nump, vecs);
		return;
	}

	front = back = qfalse;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = qtrue;
			sides[i] = SIDE_FRONT;
		}
		else if (d < ON_EPSILON)
		{
			back = qtrue;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon (newc[0], newv[0][0], stage+1);
	ClipSkyPolygon (newc[1], newv[1][0], stage+1);
}


void R_DrawSkyChain (msurface_t *s)
{
msurface_t	*fa;
int		i;
vec3_t	verts[MAX_CLIP_VERTS];
glpoly_t	*p;

if (r_skybox.value) // if the skybox value is one, draw the skybox
	{
		c_sky = 0;
		GL_Bind(solidskytexture);
		// calculate vertex values for sky box
		for (fa=s ; fa ; fa=fa->texturechain)
		{
			for (p=fa->polys ; p ; p=p->next)
			{
				for (i=0 ; p->numverts ; i++)
				{
					VectorSubtract (p->verts[i], r_origin, verts[i]);
				}
				ClipSkyPolygon (p->numverts, verts[0], 0);
			}
		}
	}
else // otherwise, draw the normal quake sky
	{
	// used when gl_texsort is on
	GL_Bind(solidskytexture);
	speedscale = realtime*8;
	speedscale -= (int)speedscale & ~127 ;
	
	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa);

	glEnable (GL_BLEND);
	GL_Bind (alphaskytexture);
	speedscale = realtime*16;
	speedscale -= (int)speedscale & ~127 ;
	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa);
	glDisable (GL_BLEND);
	}
  }
//Diabolickal Skybox Support End  
  
//===============================================================

/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (miptex_t *mt)
{
	int			i, j, p;
	byte		*src;
	unsigned	trans[16384];
	unsigned	transpix;
	int			r, g, b;
	unsigned	*rgba;
	
	src = (byte *)mt + mt->offsets[0];

	// make an average value for the back to avoid
	// a fringe on the top level

	r = g = b = 0;
	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*256 + j + 128];
			rgba = &d_8to24table[p];
			trans[(i*128) + j] = *rgba;
			r += ((byte *)rgba)[0];
			g += ((byte *)rgba)[1];
			b += ((byte *)rgba)[2];
		}

	((byte *)&transpix)[0] = r/(16384);
	((byte *)&transpix)[1] = g/(16384);
	((byte *)&transpix)[2] = b/(16384);
	((byte *)&transpix)[3] = 0;


	if (!solidskytexture)
		solidskytexture = texture_extension_number++;
	GL_Bind (solidskytexture );
	
	glTexImage2D (GL_TEXTURE_2D, 0, gl_solid_format, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*256 + j];
			if (p == 0)
				trans[(i*128) + j] = transpix;
			else
				trans[(i*128) + j] = d_8to24table[p];
		}

	if (!alphaskytexture)
		alphaskytexture = texture_extension_number++;
	GL_Bind(alphaskytexture);
	
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

//Diabolickal Skybox Support Start

/*
==================
R_LoadSkys
==================
*/

char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
void R_LoadSkys (char *skyname)
{
	int		i;
	FILE	*f;
	char	name[64];
	if (name[0] == '0')
		return;
	for (i=0 ; i<6 ; i++)
	{
		GL_Bind(SKY_TEX + i);
		int mark = Hunk_LowMark ();
		sprintf (name, "gfx/env/%s%s.tga", skyname, suf[i]);
		skyimage[i] =  loadtextureimage(name, 0, 0, qfalse, qfalse);
		if(!skyimage[i])
			Con_Printf("No sky image: %s\n", name);
		Hunk_FreeToLowMark (mark);
	}
	if(skyimage[0]&&skyimage[1]&&skyimage[2]&&skyimage[3]&&skyimage[4]&&skyimage[5])
		Con_Printf("Sucessfully Loaded All Skybox Textures: %s\n", skyname);
}

/*
=================
Sky_NewMap
=================
*/
void Sky_NewMap (void)
{
	char	key[128], value[128];
	char	*data;
    //purge old sky textures	
	
	//
	// initially no sky
	//
	//R_LoadSkys (""); //not used
	//
	// read worldspawn (this is so ugly, and shouldn't it be done on the server?)
	//
	data = cl.worldmodel->entities;
	if (!data)
		return; //FIXME: how could this possibly ever happen? -- if there's no
	// worldspawn then the sever wouldn't send the loadmap message to the client
	data = COM_Parse(data);

	if (!data) //should never happen
		return; // error

	if (com_token[0] != '{') //should never happen
		return; // error
		
	while (1)
	{
		data = COM_Parse(data);

		if (!data)
			return; // error

		if (com_token[0] == '}')
			break; // end of worldspawn

		if (com_token[0] == '_')
			strcpy(key, com_token + 1);
		else
			strcpy(key, com_token);
		while (key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;

		data = COM_Parse(data);
		if (!data)
			return; // error

		strcpy(value, com_token);
        if (!strcmp("sky", key))
            R_LoadSkys(value);
        else if (!strcmp("skyname", key)) //half-life
            R_LoadSkys(value);
        else if (!strcmp("qlsky", key)) //quake lives
            R_LoadSkys(value);
	}
}


/*

==============

R_DrawSkyBox

==============

*/
void R_DrawSkyBox (void)

{
	int		i, j, k;
	vec3_t	v;
	float	s, t;

#if 0
glEnable (GL_BLEND);
glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
glColor4f (1,1,1,0.5);
glDisable (GL_DEPTH_TEST);
#endif
	for (i=0 ; i<6 ; i++)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;
		GL_Bind (SKY_TEX+skytexorder[i]);
#if 0
skymins[0][i] = -1;
skymins[1][i] = -1;
skymaxs[0][i] = 1;
skymaxs[1][i] = 1;
#endif
		glBegin (GL_QUADS);
		MakeSkyVec (skymins[0][i], skymins[1][i], i);
		MakeSkyVec (skymins[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymins[1][i], i);
		glEnd ();
	}
#if 0
glDisable (GL_BLEND);
glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
glColor4f (1,1,1,0.5);
glEnable (GL_DEPTH_TEST);
#endif

	glPopMatrix();
}

//Diabolickal Skybox Support End
