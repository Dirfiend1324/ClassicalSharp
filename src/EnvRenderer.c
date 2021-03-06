#include "EnvRenderer.h"
#include "ExtMath.h"
#include "World.h"
#include "Funcs.h"
#include "GraphicsAPI.h"
#include "Physics.h"
#include "Block.h"
#include "Platform.h"
#include "Event.h"
#include "Utils.h"
#include "Game.h"
#include "ErrorHandler.h"
#include "Stream.h"
#include "Block.h"
#include "GraphicsCommon.h"
#include "Event.h"
#include "TerrainAtlas.h"
#include "Platform.h"
#include "Camera.h"
#include "Particle.h"

#define ENV_SMALL_VERTICES 4096
static Real32 EnvRenderer_BlendFactor(Real32 x) {
	/* return -0.05 + 0.22 * (Math_Log(x) * 0.25f); */
	Real64 blend = -0.13 + 0.28 * (Math_Log(x) * 0.25);
	if (blend < 0.0) blend = 0.0;
	if (blend > 1.0) blend = 1.0;
	return (Real32)blend;
}

#define EnvRenderer_AxisSize() (EnvRenderer_Legacy ? 128 : 65536)
/* Returns the number of vertices needed to subdivide a quad */
Int32 EnvRenderer_Vertices(Int32 axis1Len, Int32 axis2Len) {
	Int32 axisSize = EnvRenderer_AxisSize();
	return Math_CeilDiv(axis1Len, axisSize) * Math_CeilDiv(axis2Len, axisSize) * 4;
}


/*########################################################################################################################*
*------------------------------------------------------------Fog----------------------------------------------------------*
*#########################################################################################################################*/
static void EnvRenderer_CalcFog(Real32* density, PackedCol* col) {
	Vector3 pos = Game_CurrentCameraPos; Vector3I coords;
	Vector3I_Floor(&coords, &pos);     /* coords = floor(pos); */
	Vector3I_ToVector3(&pos, &coords); /* pos = coords; */

	BlockID block = World_SafeGetBlock_3I(coords);
	struct AABB blockBB;
	Vector3_Add(&blockBB.Min, &pos, &Block_MinBB[block]);
	Vector3_Add(&blockBB.Max, &pos, &Block_MaxBB[block]);

	if (AABB_ContainsPoint(&blockBB, &pos) && Block_FogDensity[block] != 0.0f) {
		*density = Block_FogDensity[block];
		*col = Block_FogCol[block];
	} else {
		*density = 0.0f;
		/* Blend fog and sky together */
		Real32 blend = EnvRenderer_BlendFactor((Real32)Game_ViewDistance);
		*col = PackedCol_Lerp(WorldEnv_FogCol, WorldEnv_SkyCol, blend);
	}
}

static void EnvRenderer_UpdateFogMinimal(Real32 fogDensity) {
	/* TODO: rewrite this to avoid raising the event? want to avoid recreating vbos too many times often */
	if (fogDensity != 0.0f) {
		/* Exp fog mode: f = e^(-density*coord) */
		/* Solve coord for f = 0.05 (good approx for fog end) */
		/*   i.e. log(0.05) = -density * coord */

		#define LOG_005 -2.99573227355399
		Real64 dist = LOG_005 / -fogDensity;
		Game_SetViewDistance((Int32)dist, false);
	} else {
		Game_SetViewDistance(Game_UserViewDistance, false);
	}
}

static void EnvRenderer_UpdateFogNormal(Real32 fogDensity, PackedCol fogCol) {
	if (fogDensity != 0.0f) {
		Gfx_SetFogMode(FOG_EXP);
		Gfx_SetFogDensity(fogDensity);
	} else if (WorldEnv_ExpFog) {
		Gfx_SetFogMode(FOG_EXP);
		/* f = 1-z/end   f = e^(-dz)
		   solve for f = 0.01 gives:
		   e^(-dz)=0.01 --> -dz=ln(0.01)
		   0.99=z/end   --> z=end*0.99
		     therefore
		  d = -ln(0.01)/(end*0.99) */

		#define LOG_001 -4.60517018598809
		Real64 density = -(LOG_001) / (Game_ViewDistance * 0.99);
		Gfx_SetFogDensity((Real32)density);
	} else {
		Gfx_SetFogMode(FOG_LINEAR);
		Gfx_SetFogEnd((Real32)Game_ViewDistance);
	}
	Gfx_SetFogCol(fogCol);
}

void EnvRenderer_UpdateFog(void) {
	Real32 fogDensity; PackedCol fogCol;
	EnvRenderer_CalcFog(&fogDensity, &fogCol);
	Gfx_ClearCol(fogCol);

	if (!World_Blocks) return;
	if (EnvRenderer_Minimal) {
		EnvRenderer_UpdateFogMinimal(fogDensity);
	} else {
		EnvRenderer_UpdateFogNormal(fogDensity, fogCol);
	}
}


/*########################################################################################################################*
*----------------------------------------------------------Clouds---------------------------------------------------------*
*#########################################################################################################################*/
GfxResourceID clouds_vb, clouds_tex;
Int32 clouds_vertices;

void EnvRenderer_RenderClouds(Real64 deltaTime) {
	if (!clouds_vb || WorldEnv_CloudsHeight < -2000) return;
	Real64 time = Game_Accumulator;
	Real32 offset = (Real32)(time / 2048.0f * 0.6f * WorldEnv_CloudsSpeed);

	Gfx_SetMatrixMode(MATRIX_TYPE_TEXTURE);
	struct Matrix m = Matrix_Identity; m.Row3.X = offset; /* translate X axis */
	Gfx_LoadMatrix(&m);
	Gfx_SetMatrixMode(MATRIX_TYPE_VIEW);

	Gfx_SetAlphaTest(true);
	Gfx_SetTexturing(true);
	Gfx_BindTexture(clouds_tex);
	Gfx_SetBatchFormat(VERTEX_FORMAT_P3FT2FC4B);
	Gfx_BindVb(clouds_vb);
	Gfx_DrawVb_IndexedTris(clouds_vertices);
	Gfx_SetAlphaTest(false);
	Gfx_SetTexturing(false);

	Gfx_SetMatrixMode(MATRIX_TYPE_TEXTURE);
	Gfx_LoadIdentityMatrix();
	Gfx_SetMatrixMode(MATRIX_TYPE_VIEW);
}

static void EnvRenderer_DrawCloudsY(Int32 x1, Int32 z1, Int32 x2, Int32 z2, Int32 y, VertexP3fT2fC4b* vertices) {
	Int32 endX = x2, endZ = z2, startZ = z1, axisSize = EnvRenderer_AxisSize();
	/* adjust range so that largest negative uv coordinate is shifted to 0 or above. */
	Real32 offset = (Real32)Math_CeilDiv(-x1, 2048);
	VertexP3fT2fC4b v;
	v.Y = (Real32)y + 0.1f; v.Col = WorldEnv_CloudsCol;

	for (; x1 < endX; x1 += axisSize) {
		x2 = x1 + axisSize;
		if (x2 > endX) x2 = endX;
		z1 = startZ;
		for (; z1 < endZ; z1 += axisSize) {
			z2 = z1 + axisSize;
			if (z2 > endZ) z2 = endZ;

			Real32 u1 = (Real32)x1 / 2048.0f + offset, u2 = (Real32)x2 / 2048.0f + offset;
			Real32 v1 = (Real32)z1 / 2048.0f + offset, v2 = (Real32)z2 / 2048.0f + offset;
			v.X = (Real32)x1; v.Z = (Real32)z1; v.U = u1; v.V = v1; *vertices++ = v;
			                  v.Z = (Real32)z2;           v.V = v2; *vertices++ = v;
			v.X = (Real32)x2;                   v.U = u2;           *vertices++ = v;
			                  v.Z = (Real32)z1;           v.V = v1; *vertices++ = v;
		}
	}
}

static void EnvRenderer_UpdateClouds(void) {
	if (!World_Blocks || Gfx_LostContext) return;
	Gfx_DeleteVb(&clouds_vb);
	if (EnvRenderer_Minimal) return;

	Int32 extent = Utils_AdjViewDist(Game_ViewDistance);
	Int32 x1 = -extent, x2 = World_Width + extent;
	Int32 z1 = -extent, z2 = World_Length + extent;
	clouds_vertices = EnvRenderer_Vertices(x2 - x1, z2 - z1);

	VertexP3fT2fC4b v[ENV_SMALL_VERTICES];
	VertexP3fT2fC4b* ptr = v;
	if (clouds_vertices > ENV_SMALL_VERTICES) {
		ptr = Mem_Alloc(clouds_vertices, sizeof(VertexP3fT2fC4b), "temp clouds vertices");
	}

	EnvRenderer_DrawCloudsY(x1, z1, x2, z2, WorldEnv_CloudsHeight, ptr);
	clouds_vb = Gfx_CreateVb(ptr, VERTEX_FORMAT_P3FT2FC4B, clouds_vertices);

	if (clouds_vertices > ENV_SMALL_VERTICES) Mem_Free(&ptr);
}


/*########################################################################################################################*
*------------------------------------------------------------Sky----------------------------------------------------------*
*#########################################################################################################################*/
GfxResourceID sky_vb;
Int32 sky_vertices;

void EnvRenderer_RenderSky(Real64 deltaTime) {
	if (!sky_vb || EnvRenderer_ShouldRenderSkybox()) return;
	Vector3 pos = Game_CurrentCameraPos;
	Real32 normalY = (Real32)World_Height + 8.0f;
	Real32 skyY = max(pos.Y + 8.0f, normalY);
	Gfx_SetBatchFormat(VERTEX_FORMAT_P3FC4B);
	Gfx_BindVb(sky_vb);

	if (skyY == normalY) {
		Gfx_DrawVb_IndexedTris(sky_vertices);
	} else {
		struct Matrix m = Gfx_View;
		Real32 dy = skyY - normalY; /* inlined Y translation matrix multiply */
		m.Row3.X += dy * m.Row1.X; m.Row3.Y += dy * m.Row1.Y;
		m.Row3.Z += dy * m.Row1.Z; m.Row3.W += dy * m.Row1.W;

		Gfx_LoadMatrix(&m);
		Gfx_DrawVb_IndexedTris(sky_vertices);
		Gfx_LoadMatrix(&Gfx_View);
	}
}

static void EnvRenderer_DrawSkyY(Int32 x1, Int32 z1, Int32 x2, Int32 z2, Int32 y, VertexP3fC4b* vertices) {
	Int32 endX = x2, endZ = z2, startZ = z1, axisSize = EnvRenderer_AxisSize();
	VertexP3fC4b v;
	v.Y = (Real32)y; v.Col = WorldEnv_SkyCol;

	for (; x1 < endX; x1 += axisSize) {
		x2 = x1 + axisSize;
		if (x2 > endX) x2 = endX;
		z1 = startZ;
		for (; z1 < endZ; z1 += axisSize) {
			z2 = z1 + axisSize;
			if (z2 > endZ) z2 = endZ;

			v.X = (Real32)x1; v.Z = (Real32)z1; *vertices++ = v;
			                  v.Z = (Real32)z2; *vertices++ = v;
			v.X = (Real32)x2;                   *vertices++ = v;
			                  v.Z = (Real32)z1; *vertices++ = v;
		}
	}
}

static void EnvRenderer_UpdateSky(void) {
	if (!World_Blocks || Gfx_LostContext) return;
	Gfx_DeleteVb(&sky_vb);
	if (EnvRenderer_Minimal) return;

	Int32 extent = Utils_AdjViewDist(Game_ViewDistance);
	Int32 x1 = -extent, x2 = World_Width + extent;
	Int32 z1 = -extent, z2 = World_Length + extent;
	sky_vertices = EnvRenderer_Vertices(x2 - x1, z2 - z1);

	VertexP3fC4b v[ENV_SMALL_VERTICES];
	VertexP3fC4b* ptr = v;
	if (sky_vertices > ENV_SMALL_VERTICES) {
		ptr = Mem_Alloc(sky_vertices, sizeof(VertexP3fC4b), "temp sky vertices");
	}

	Int32 height = max((World_Height + 2) + 6, WorldEnv_CloudsHeight + 6);
	EnvRenderer_DrawSkyY(x1, z1, x2, z2, height, ptr);
	sky_vb = Gfx_CreateVb(ptr, VERTEX_FORMAT_P3FC4B, sky_vertices);

	if (sky_vertices > ENV_SMALL_VERTICES) Mem_Free(&ptr);
}

/*########################################################################################################################*
*----------------------------------------------------------Skybox---------------------------------------------------------*
*#########################################################################################################################*/
GfxResourceID skybox_tex, skybox_vb;
#define SKYBOX_COUNT (6 * 4)
bool EnvRenderer_ShouldRenderSkybox(void) { return skybox_tex && !EnvRenderer_Minimal; }

void EnvRenderer_RenderSkybox(Real64 deltaTime) {
	if (!skybox_vb) return;
	Gfx_SetDepthWrite(false);
	Gfx_SetTexturing(true);
	Gfx_BindTexture(skybox_tex);
	Gfx_SetBatchFormat(VERTEX_FORMAT_P3FT2FC4B);

	struct Matrix m = Matrix_Identity, rotX, rotY, view;

	/* Base skybox rotation */
	Real32 rotTime = (Real32)(Game_Accumulator * 2 * MATH_PI); /* So speed of 1 rotates whole skybox every second */
	Matrix_RotateY(&rotY, WorldEnv_SkyboxHorSpeed * rotTime); Matrix_MulBy(&m, &rotY);
	Matrix_RotateX(&rotX, WorldEnv_SkyboxVerSpeed * rotTime); Matrix_MulBy(&m, &rotX);

	/* Rotate around camera */
	Vector3 pos = Game_CurrentCameraPos, zero = Vector3_Zero;
	Game_CurrentCameraPos = zero;
	Camera_Active->GetView(&view); Matrix_MulBy(&m, &view);
	Game_CurrentCameraPos = pos;

	Gfx_LoadMatrix(&m);
	Gfx_BindVb(skybox_vb);
	Gfx_DrawVb_IndexedTris(SKYBOX_COUNT);

	Gfx_SetTexturing(false);
	Gfx_LoadMatrix(&Gfx_View);
	Gfx_SetDepthWrite(true);
}

static void EnvRenderer_UpdateSkybox(void) {
	if (Gfx_LostContext) return;
	Gfx_DeleteVb(&skybox_vb);
	if (EnvRenderer_Minimal) return;

	static VertexP3fT2fC4b vertices[SKYBOX_COUNT] = {
		/* Front quad */
		{ -1, -1, -1, {0,0,0,0}, 0.25f, 1.00f }, {  1, -1, -1, {0,0,0,0}, 0.50f, 1.00f },
		{  1,  1, -1, {0,0,0,0}, 0.50f, 0.50f }, { -1,  1, -1, {0,0,0,0}, 0.25f, 0.50f },
		/* Left quad */
		{ -1, -1,  1, {0,0,0,0}, 0.00f, 1.00f }, { -1, -1, -1, {0,0,0,0}, 0.25f, 1.00f },
		{ -1,  1, -1, {0,0,0,0}, 0.25f, 0.50f }, { -1,  1,  1, {0,0,0,0}, 0.00f, 0.50f },
		/* Back quad */
		{  1, -1,  1, {0,0,0,0}, 0.75f, 1.00f }, { -1, -1,  1, {0,0,0,0}, 1.00f, 1.00f },
		{ -1,  1,  1, {0,0,0,0}, 1.00f, 0.50f }, {  1,  1,  1, {0,0,0,0}, 0.75f, 0.50f },
		/* Right quad */
		{  1, -1, -1, {0,0,0,0}, 0.50f, 1.00f }, {  1, -1,  1, {0,0,0,0}, 0.75f, 1.00f },
		{  1,  1,  1, {0,0,0,0}, 0.75f, 0.50f }, {  1,  1, -1, {0,0,0,0}, 0.50f, 0.50f },
		/* Top quad */
		{  1,  1, -1, {0,0,0,0}, 0.50f, 0.50f }, {  1,  1,  1, {0,0,0,0}, 0.50f, 0.00f },
		{ -1,  1,  1, {0,0,0,0}, 0.25f, 0.00f }, { -1,  1, -1, {0,0,0,0}, 0.25f, 0.50f },
		/* Bottom quad */
		{  1, -1, -1, {0,0,0,0}, 0.75f, 0.50f }, {  1, -1,  1, {0,0,0,0}, 0.75f, 0.00f },
		{ -1, -1,  1, {0,0,0,0}, 0.50f, 0.00f }, { -1, -1, -1, {0,0,0,0}, 0.50f, 0.50f },
	};

	Int32 i;
	for (i = 0; i < SKYBOX_COUNT; i++) { vertices[i].Col = WorldEnv_CloudsCol; }
	skybox_vb = Gfx_CreateVb(vertices, VERTEX_FORMAT_P3FT2FC4B, SKYBOX_COUNT);
}


/*########################################################################################################################*
*----------------------------------------------------------Weather--------------------------------------------------------*
*#########################################################################################################################*/
GfxResourceID rain_tex, snow_tex, weather_vb;
#define WEATHER_EXTENT 4
#define WEATHER_VERTS_COUNT 8 * (WEATHER_EXTENT * 2 + 1) * (WEATHER_EXTENT * 2 + 1)

Real64 weather_accumulator;
Vector3I weather_lastPos;

static void EnvRenderer_InitWeatherHeightmap(void) {
	Weather_Heightmap = Mem_Alloc(World_Width * World_Length, sizeof(Int16), "weather heightmap");
	Int32 i;
	for (i = 0; i < World_Width * World_Length; i++) {
		Weather_Heightmap[i] = Int16_MaxValue;
	}
}

static Int32 EnvRenderer_CalcRainHeightAt(Int32 x, Int32 maxY, Int32 z, Int32 index) {
	Int32 i = World_Pack(x, maxY, z), y;

	for (y = maxY; y >= 0; y--, i -= World_OneY) {
		UInt8 draw = Block_Draw[World_Blocks[i]];
		if (!(draw == DRAW_GAS || draw == DRAW_SPRITE)) {
			Weather_Heightmap[index] = y;
			return y;
		}
	}
	Weather_Heightmap[index] = -1;
	return -1;
}

static Real32 EnvRenderer_RainHeight(Int32 x, Int32 z) {
	if (x < 0 || z < 0 || x >= World_Width || z >= World_Length) {
		return (Real32)WorldEnv_EdgeHeight;
	}
	Int32 index = (x * World_Length) + z;
	Int32 height = Weather_Heightmap[index];

	Int32 y = height == Int16_MaxValue ? EnvRenderer_CalcRainHeightAt(x, World_MaxY, z, index) : height;
	return y == -1 ? 0 : y + Block_MaxBB[World_GetBlock(x, y, z)].Y;
}

void EnvRenderer_OnBlockChanged(Int32 x, Int32 y, Int32 z, BlockID oldBlock, BlockID newBlock) {
	bool didBlock = !(Block_Draw[oldBlock] == DRAW_GAS || Block_Draw[oldBlock] == DRAW_SPRITE);
	bool nowBlock = !(Block_Draw[newBlock] == DRAW_GAS || Block_Draw[newBlock] == DRAW_SPRITE);
	if (didBlock == nowBlock) return;

	Int32 index = (x * World_Length) + z;
	Int32 height = Weather_Heightmap[index];
	/* Two cases can be skipped here: */
	/* a) rain height was not calculated to begin with (height is short.MaxValue) */
	/* b) changed y is below current calculated rain height */
	if (y < height) return;

	if (nowBlock) {
		/* Simple case: Rest of column below is now not visible to rain. */
		Weather_Heightmap[index] = y;
	} else {
		/* Part of the column is now visible to rain, we don't know how exactly how high it should be though. */
		/* However, we know that if the old block was above or equal to rain height, then the new rain height must be <= old block.y */
		EnvRenderer_CalcRainHeightAt(x, y, z, index);
	}
}

static Real32 EnvRenderer_RainAlphaAt(Real32 x) {
	/* Wolfram Alpha: fit {0,178},{1,169},{4,147},{9,114},{16,59},{25,9} */
	Real32 falloff = 0.05f * x * x - 7 * x;
	return 178 + falloff * WorldEnv_WeatherFade;
}

void EnvRenderer_RenderWeather(Real64 deltaTime) {
	Int32 weather = WorldEnv_Weather;
	if (weather == WEATHER_SUNNY) return;
	if (!Weather_Heightmap) EnvRenderer_InitWeatherHeightmap();

	Gfx_BindTexture(weather == WEATHER_RAINY ? rain_tex : snow_tex);
	Vector3 camPos = Game_CurrentCameraPos;
	Vector3I pos;
	Vector3I_Floor(&pos, &camPos);
	bool moved = Vector3I_NotEquals(&pos, &weather_lastPos);
	weather_lastPos = pos;

	/* Rain should extend up by 64 blocks, or to the top of the world. */
	pos.Y += 64;
	pos.Y = max(World_Height, pos.Y);

	Real32 speed = (weather == WEATHER_RAINY ? 1.0f : 0.2f) * WorldEnv_WeatherSpeed;
	Real32 vOffset = (Real32)Game_Accumulator * speed;
	weather_accumulator += deltaTime;
	bool particles = weather == WEATHER_RAINY;

	PackedCol col = WorldEnv_SunCol;
	VertexP3fT2fC4b v;
	VertexP3fT2fC4b vertices[WEATHER_VERTS_COUNT];
	VertexP3fT2fC4b* ptr = vertices;

	Int32 dx, dz;
	for (dx = -WEATHER_EXTENT; dx <= WEATHER_EXTENT; dx++) {
		for (dz = -WEATHER_EXTENT; dz <= WEATHER_EXTENT; dz++) {
			Int32 x = pos.X + dx, z = pos.Z + dz;
			Real32 y = EnvRenderer_RainHeight(x, z);
			Real32 height = pos.Y - y;
			if (height <= 0) continue;

			if (particles && (weather_accumulator >= 0.25 || moved)) {
				Vector3 particlePos = Vector3_Create3((Real32)x, y, (Real32)z);
				Particles_RainSnowEffect(particlePos);
			}

			Real32 dist = (Real32)dx * (Real32)dx + (Real32)dz * (Real32)dz;
			Real32 alpha = EnvRenderer_RainAlphaAt(dist);
			/* Clamp between 0 and 255 */
			alpha = alpha < 0.0f ? 0.0f : alpha;
			alpha = alpha > 255.0f ? 255.0f : alpha;
			col.A = (UInt8)alpha;

			/* NOTE: Making vertex is inlined since this is called millions of times. */
			v.Col = col;
			Real32 worldV = vOffset + (z & 1) / 2.0f - (x & 0x0F) / 16.0f;
			Real32 v1 = y / 6.0f + worldV, v2 = (y + height) / 6.0f + worldV;
			Real32 x1 = (Real32)x,       y1 = (Real32)y,            z1 = (Real32)z;
			Real32 x2 = (Real32)(x + 1), y2 = (Real32)(y + height), z2 = (Real32)(z + 1);

			v.X = x1; v.Y = y1; v.Z = z1; v.U = 0.0f; v.V = v1; *ptr++ = v;
			          v.Y = y2;                       v.V = v2; *ptr++ = v;
			v.X = x2;           v.Z = z2; v.U = 1.0f; 	        *ptr++ = v;
			          v.Y = y1;                       v.V = v1; *ptr++ = v;

			                    v.Z = z1;					    *ptr++ = v;
			          v.Y = y2;                       v.V = v2; *ptr++ = v;
			v.X = x1;           v.Z = z2; v.U = 0.0f;		    *ptr++ = v;
			          v.Y = y1;                       v.V = v1; *ptr++ = v;
		}
	}

	if (particles && (weather_accumulator >= 0.25f || moved)) {
		weather_accumulator = 0;
	}
	if (ptr == vertices) return;

	Gfx_SetAlphaTest(false);
	Gfx_SetDepthWrite(false);
	Gfx_SetAlphaArgBlend(true);

	Gfx_SetBatchFormat(VERTEX_FORMAT_P3FT2FC4B);
	Int32 vCount = (Int32)(ptr - vertices);
	GfxCommon_UpdateDynamicVb_IndexedTris(weather_vb, vertices, vCount);

	Gfx_SetAlphaArgBlend(false);
	Gfx_SetDepthWrite(true);
	Gfx_SetAlphaTest(false);
}


/*########################################################################################################################*
*--------------------------------------------------------Sides/Edge-------------------------------------------------------*
*#########################################################################################################################*/
GfxResourceID sides_vb, edges_vb, sides_tex, edges_tex;
Int32 sides_vertices, edges_vertices;
bool sides_fullBright, edges_fullBright;
TextureLoc edges_lastTexLoc, sides_lastTexLoc;

void EnvRenderer_RenderBorders(BlockID block, GfxResourceID vb, GfxResourceID tex, Int32 count) {
	if (!vb) return;

	Gfx_SetTexturing(true);
	GfxCommon_SetupAlphaState(Block_Draw[block]);
	Gfx_EnableMipmaps();

	Gfx_BindTexture(tex);
	Gfx_SetBatchFormat(VERTEX_FORMAT_P3FT2FC4B);
	Gfx_BindVb(vb);
	if (count) Gfx_DrawVb_IndexedTris(count);

	Gfx_DisableMipmaps();
	GfxCommon_RestoreAlphaState(Block_Draw[block]);
	Gfx_SetTexturing(false);
}

void EnvRenderer_RenderMapSides(Real64 delta) {
	EnvRenderer_RenderBorders(WorldEnv_SidesBlock, 
		sides_vb, sides_tex, sides_vertices);
}

void EnvRenderer_RenderMapEdges(Real64 delta) {
	/* Do not draw water when player cannot see it */
	/* Fixes some 'depth bleeding through' issues with 16 bit depth buffers on large maps */
	Vector3 camPos = Game_CurrentCameraPos;
	Int32 vertices = 0, yVisible = min(0, WorldEnv_SidesHeight);
	if (camPos.Y >= yVisible) vertices = edges_vertices;

	EnvRenderer_RenderBorders(WorldEnv_EdgeBlock,
		edges_vb, edges_tex, vertices);
}

static void EnvRenderer_MakeBorderTex(GfxResourceID* texId, BlockID block) {
	TextureLoc texLoc = Block_GetTexLoc(block, FACE_YMAX);
	if (Gfx_LostContext) return;

	Gfx_DeleteTexture(texId);
	*texId = Atlas2D_LoadTile(texLoc);
}

static void EnvRenderer_CalcBorderRects(struct Rectangle2D* rects) {
	Int32 extent = Utils_AdjViewDist(Game_ViewDistance);
	rects[0] = Rectangle2D_Make(-extent, -extent, extent + World_Width + extent, extent);
	rects[1] = Rectangle2D_Make(-extent, World_Length, extent + World_Width + extent, extent);

	rects[2] = Rectangle2D_Make(-extent, 0, extent, World_Length);
	rects[3] = Rectangle2D_Make(World_Width, 0, extent, World_Length);
}

static void EnvRenderer_UpdateBorderTextures(void) {
	EnvRenderer_MakeBorderTex(&edges_tex, WorldEnv_EdgeBlock);
	EnvRenderer_MakeBorderTex(&sides_tex, WorldEnv_SidesBlock);
}

#define borders_HorOffset(block) (Block_RenderMinBB[block].X - Block_MinBB[block].X)
#define borders_YOffset(block) (Block_RenderMinBB[block].Y - Block_MinBB[block].Y)

static void EnvRenderer_DrawBorderX(Int32 x, Int32 z1, Int32 z2, Int32 y1, Int32 y2, PackedCol col, VertexP3fT2fC4b** vertices) {
	Int32 endZ = z2, endY = y2, startY = y1, axisSize = EnvRenderer_AxisSize();
	VertexP3fT2fC4b* ptr = *vertices;
	VertexP3fT2fC4b v;
	v.X = (Real32)x; v.Col = col;

	for (; z1 < endZ; z1 += axisSize) {
		z2 = z1 + axisSize;
		if (z2 > endZ) z2 = endZ;
		y1 = startY;
		for (; y1 < endY; y1 += axisSize) {
			y2 = y1 + axisSize;
			if (y2 > endY) y2 = endY;

			Real32 u2 = (Real32)z2 - (Real32)z1, v2 = (Real32)y2 - (Real32)y1;
			v.Y = (Real32)y1; v.Z = (Real32)z1; v.U = 0.0f; v.V = v2;   *ptr++ = v;
			v.Y = (Real32)y2;                               v.V = 0.0f; *ptr++ = v;
			                  v.Z = (Real32)z2; v.U = u2;               *ptr++ = v;
			v.Y = (Real32)y1;                               v.V = v2;   *ptr++ = v;
		}
	}
	*vertices = ptr;
}

static void EnvRenderer_DrawBorderZ(Int32 z, Int32 x1, Int32 x2, Int32 y1, Int32 y2, PackedCol col, VertexP3fT2fC4b** vertices) {
	Int32 endX = x2, endY = y2, startY = y1, axisSize = EnvRenderer_AxisSize();
	VertexP3fT2fC4b* ptr = *vertices;
	VertexP3fT2fC4b v;
	v.Z = (Real32)z; v.Col = col;

	for (; x1 < endX; x1 += axisSize) {
		x2 = x1 + axisSize;
		if (x2 > endX) x2 = endX;
		y1 = startY;
		for (; y1 < endY; y1 += axisSize) {
			y2 = y1 + axisSize;
			if (y2 > endY) y2 = endY;

			Real32 u2 = (Real32)x2 - (Real32)x1, v2 = (Real32)y2 - (Real32)y1;
			v.X = (Real32)x1; v.Y = (Real32)y1; v.U = 0.0f; v.V = v2;   *ptr++ = v;
			                  v.Y = (Real32)y2;             v.V = 0.0f; *ptr++ = v;
			v.X = (Real32)x2;                   v.U = u2;               *ptr++ = v;
			                  v.Y = (Real32)y1;             v.V = v2;   *ptr++ = v;
		}
	}
	*vertices = ptr;
}

static void EnvRenderer_DrawBorderY(Int32 x1, Int32 z1, Int32 x2, Int32 z2, Real32 y, PackedCol col, Real32 offset, Real32 yOffset, VertexP3fT2fC4b** vertices) {
	Int32 endX = x2, endZ = z2, startZ = z1, axisSize = EnvRenderer_AxisSize();
	VertexP3fT2fC4b* ptr = *vertices;
	VertexP3fT2fC4b v;
	v.Y = y + yOffset; v.Col = col;

	for (; x1 < endX; x1 += axisSize) {
		x2 = x1 + axisSize;
		if (x2 > endX) x2 = endX;
		z1 = startZ;
		for (; z1 < endZ; z1 += axisSize) {
			z2 = z1 + axisSize;
			if (z2 > endZ) z2 = endZ;

			Real32 u2 = (Real32)x2 - (Real32)x1, v2 = (Real32)z2 - (Real32)z1;
			v.X = (Real32)x1 + offset; v.Z = (Real32)z1 + offset; v.U = 0.0f; v.V = 0.0f; *ptr++ = v;
			                           v.Z = (Real32)z2 + offset;             v.V = v2;   *ptr++ = v;
			v.X = (Real32)x2 + offset;                            v.U = u2;               *ptr++ = v;
			                           v.Z = (Real32)z1 + offset;             v.V = 0.0f; *ptr++ = v;
		}
	}
	*vertices = ptr;
}

static void EnvRenderer_UpdateMapSides(void) {
	if (!World_Blocks || Gfx_LostContext) return;
	Gfx_DeleteVb(&sides_vb);
	BlockID block = WorldEnv_SidesBlock;

	if (Block_Draw[block] == DRAW_GAS) return;
	struct Rectangle2D rects[4]; 
	EnvRenderer_CalcBorderRects(rects);

	Int32 i; sides_vertices = 0;
	for (i = 0; i < 4; i++) {
		struct Rectangle2D r = rects[i];
		sides_vertices += EnvRenderer_Vertices(r.Width, r.Height); /* YQuads outside */
	}

	Int32 y = WorldEnv_SidesHeight;
	sides_vertices += EnvRenderer_Vertices(World_Width, World_Length); /* YQuads beneath map */
	sides_vertices += 2 * EnvRenderer_Vertices(World_Width, Math_AbsI(y)); /* ZQuads */
	sides_vertices += 2 * EnvRenderer_Vertices(World_Length, Math_AbsI(y)); /* XQuads */

	VertexP3fT2fC4b v[ENV_SMALL_VERTICES];
	VertexP3fT2fC4b* ptr = v;
	if (sides_vertices > ENV_SMALL_VERTICES) {
		ptr = Mem_Alloc(sides_vertices, sizeof(VertexP3fT2fC4b), "temp sides vertices");
	}
	VertexP3fT2fC4b* temp = ptr;

	sides_fullBright = Block_FullBright[block];
	PackedCol white = PACKEDCOL_WHITE;
	PackedCol col = sides_fullBright ? white : WorldEnv_ShadowCol;
	Block_Tint(col, block)

	for (i = 0; i < 4; i++) {
		struct Rectangle2D r = rects[i];
		EnvRenderer_DrawBorderY(r.X, r.Y, r.X + r.Width, r.Y + r.Height, (Real32)y, col,
			0, borders_YOffset(block), &temp);
	}

	/* Work properly for when ground level is below 0 */
	Int32 y1 = 0, y2 = y;
	if (y < 0) { y1 = y; y2 = 0; }
	EnvRenderer_DrawBorderY(0, 0, World_Width, World_Length, 0, col, 0, 0, &temp);
	EnvRenderer_DrawBorderZ(0, 0, World_Width, y1, y2, col, &temp);
	EnvRenderer_DrawBorderZ(World_Length, 0, World_Width, y1, y2, col, &temp);
	EnvRenderer_DrawBorderX(0, 0, World_Length, y1, y2, col, &temp);
	EnvRenderer_DrawBorderX(World_Width, 0, World_Length, y1, y2, col, &temp);

	sides_vb = Gfx_CreateVb(ptr, VERTEX_FORMAT_P3FT2FC4B, sides_vertices);
	if (sides_vertices > ENV_SMALL_VERTICES) Mem_Free(&ptr);
}

static void EnvRenderer_UpdateMapEdges(void) {
	if (!World_Blocks || Gfx_LostContext) return;
	Gfx_DeleteVb(&edges_vb);
	BlockID block = WorldEnv_EdgeBlock;

	if (Block_Draw[block] == DRAW_GAS) return;
	struct Rectangle2D rects[4];
	EnvRenderer_CalcBorderRects(rects);

	Int32 i; edges_vertices = 0;
	for (i = 0; i < 4; i++) {
		struct Rectangle2D r = rects[i];
		edges_vertices += EnvRenderer_Vertices(r.Width, r.Height); /* YPlanes outside */
	}

	VertexP3fT2fC4b v[ENV_SMALL_VERTICES];
	VertexP3fT2fC4b* ptr = v;
	if (edges_vertices > ENV_SMALL_VERTICES) {
		ptr = Mem_Alloc(edges_vertices, sizeof(VertexP3fT2fC4b), "temp edge vertices");
	}
	VertexP3fT2fC4b* temp = ptr;

	edges_fullBright = Block_FullBright[block];
	PackedCol white = PACKEDCOL_WHITE;
	PackedCol col = edges_fullBright ? white : WorldEnv_SunCol;
	Block_Tint(col, block)

	Real32 y = (Real32)WorldEnv_EdgeHeight;
	for (i = 0; i < 4; i++) {
		struct Rectangle2D r = rects[i];
		EnvRenderer_DrawBorderY(r.X, r.Y, r.X + r.Width, r.Y + r.Height, y, col,
			borders_HorOffset(block), borders_YOffset(block), &temp);
	}

	edges_vb = Gfx_CreateVb(ptr, VERTEX_FORMAT_P3FT2FC4B, edges_vertices);
	if (edges_vertices > ENV_SMALL_VERTICES) Mem_Free(&ptr);
}


/*########################################################################################################################*
*---------------------------------------------------------General---------------------------------------------------------*
*#########################################################################################################################*/
static void EnvRenderer_DeleteVbs(void) {
	Gfx_DeleteVb(&sky_vb);
	Gfx_DeleteVb(&clouds_vb);
	Gfx_DeleteVb(&skybox_vb);
	Gfx_DeleteVb(&sides_vb);
	Gfx_DeleteVb(&edges_vb);
	Gfx_DeleteVb(&weather_vb);
}

static void EnvRenderer_ContextLost(void* obj) {
	EnvRenderer_DeleteVbs();
	Gfx_DeleteTexture(&sides_tex);
	Gfx_DeleteTexture(&edges_tex);
}

static void EnvRenderer_UpdateAll(void) {
	EnvRenderer_DeleteVbs();
	EnvRenderer_UpdateMapSides();
	EnvRenderer_UpdateMapEdges();
	EnvRenderer_UpdateClouds();
	EnvRenderer_UpdateSky();
	EnvRenderer_UpdateSkybox();
	EnvRenderer_UpdateFog();

	/* TODO: Don't allocate unless used? */
	weather_vb = Gfx_CreateDynamicVb(VERTEX_FORMAT_P3FT2FC4B, WEATHER_VERTS_COUNT);
	/* TODO: Don't need to do this on every new map */
	EnvRenderer_UpdateBorderTextures();
}

static void EnvRenderer_ContextRecreated(void* obj) {
	Gfx_SetFog(!EnvRenderer_Minimal);
	EnvRenderer_UpdateAll();
}

static void EnvRenderer_Reset(void) {
	Gfx_SetFog(false);
	EnvRenderer_DeleteVbs();
	Mem_Free(&Weather_Heightmap);
	weather_lastPos = Vector3I_MaxValue();
}

static void EnvRenderer_OnNewMapLoaded(void) {
	EnvRenderer_ContextRecreated(NULL);
}

void EnvRenderer_UseLegacyMode(bool legacy) {
	EnvRenderer_Legacy = legacy;
	EnvRenderer_ContextRecreated(NULL);
}

void EnvRenderer_UseMinimalMode(bool minimal) {
	EnvRenderer_Minimal = minimal;
	EnvRenderer_ContextRecreated(NULL);
}


static void EnvRenderer_FileChanged(void* obj, struct Stream* src, String* name) {
	if (String_CaselessEqualsConst(name, "clouds.png")) {
		Game_UpdateTexture(&clouds_tex, src, name, NULL);
	} else if (String_CaselessEqualsConst(name, "skybox.png")) {
		Game_UpdateTexture(&skybox_tex, src, name, NULL);
	} else if (String_CaselessEqualsConst(name, "snow.png")) {
		Game_UpdateTexture(&snow_tex, src, name, NULL);
	} else if (String_CaselessEqualsConst(name, "rain.png")) {
		Game_UpdateTexture(&rain_tex, src, name, NULL);
	}
}

static void EnvRenderer_TexturePackChanged(void* obj) {
	/* TODO: Find better way, really should delete them all here */
	Gfx_DeleteTexture(&skybox_tex);
}
static void EnvRenderer_TerrainAtlasChanged(void* obj) {
	EnvRenderer_UpdateBorderTextures();
}

static void EnvRenderer_ViewDistanceChanged(void* obj) {
	EnvRenderer_UpdateAll();
}

static void EnvRenderer_EnvVariableChanged(void* obj, Int32 envVar) {
	if (envVar == ENV_VAR_EDGE_BLOCK) {
		EnvRenderer_MakeBorderTex(&edges_tex, WorldEnv_EdgeBlock);
	} else if (envVar == ENV_VAR_SIDES_BLOCK) {
		EnvRenderer_MakeBorderTex(&sides_tex, WorldEnv_SidesBlock);
	} else if (envVar == ENV_VAR_EDGE_HEIGHT || envVar == ENV_VAR_SIDES_OFFSET) {
		EnvRenderer_UpdateMapEdges();
		EnvRenderer_UpdateMapSides();
	} else if (envVar == ENV_VAR_SUN_COL) {
		EnvRenderer_UpdateMapEdges();
	} else if (envVar == ENV_VAR_SHADOW_COL) {
		EnvRenderer_UpdateMapSides();
	} else if (envVar == ENV_VAR_SKY_COL) {
		EnvRenderer_UpdateSky();
	} else if (envVar == ENV_VAR_FOG_COL) {
		EnvRenderer_UpdateFog();
	} else if (envVar == ENV_VAR_CLOUDS_COL) {
		EnvRenderer_UpdateClouds();
		EnvRenderer_UpdateSkybox();
	} else if (envVar == ENV_VAR_CLOUDS_HEIGHT) {
		EnvRenderer_UpdateSky();
		EnvRenderer_UpdateClouds();
	}
}

static void EnvRenderer_Init(void) {
	Event_RegisterEntry(&TextureEvents_FileChanged, NULL, EnvRenderer_FileChanged);
	Event_RegisterVoid(&TextureEvents_PackChanged,  NULL, EnvRenderer_TexturePackChanged);
	Event_RegisterVoid(&TextureEvents_AtlasChanged, NULL, EnvRenderer_TerrainAtlasChanged);

	Event_RegisterVoid(&GfxEvents_ViewDistanceChanged, NULL, EnvRenderer_ViewDistanceChanged);
	Event_RegisterInt(&WorldEvents_EnvVarChanged,      NULL, EnvRenderer_EnvVariableChanged);
	Event_RegisterVoid(&GfxEvents_ContextLost,         NULL, EnvRenderer_ContextLost);
	Event_RegisterVoid(&GfxEvents_ContextRecreated,    NULL, EnvRenderer_ContextRecreated);

	Game_SetViewDistance(Game_UserViewDistance, false);
}

static void EnvRenderer_Free(void) {
	Event_UnregisterEntry(&TextureEvents_FileChanged, NULL, EnvRenderer_FileChanged);
	Event_UnregisterVoid(&TextureEvents_PackChanged,  NULL, EnvRenderer_TexturePackChanged);
	Event_UnregisterVoid(&TextureEvents_AtlasChanged, NULL, EnvRenderer_TerrainAtlasChanged);

	Event_UnregisterVoid(&GfxEvents_ViewDistanceChanged, NULL, EnvRenderer_ViewDistanceChanged);
	Event_UnregisterInt(&WorldEvents_EnvVarChanged,      NULL, EnvRenderer_EnvVariableChanged);
	Event_UnregisterVoid(&GfxEvents_ContextLost,         NULL, EnvRenderer_ContextLost);
	Event_UnregisterVoid(&GfxEvents_ContextRecreated,    NULL, EnvRenderer_ContextRecreated);

	EnvRenderer_ContextLost(NULL);
	Mem_Free(&Weather_Heightmap);

	Gfx_DeleteTexture(&clouds_tex);
	Gfx_DeleteTexture(&skybox_tex);
	Gfx_DeleteTexture(&rain_tex);
	Gfx_DeleteTexture(&snow_tex);
}

void EnvRenderer_MakeComponent(struct IGameComponent* comp) {
	comp->Init     = EnvRenderer_Init;
	comp->Reset    = EnvRenderer_Reset;
	comp->OnNewMap = EnvRenderer_Reset;
	comp->OnNewMapLoaded = EnvRenderer_OnNewMapLoaded;
	comp->Free     = EnvRenderer_Free;
}
