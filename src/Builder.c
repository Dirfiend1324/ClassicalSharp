#include "Builder.h"
#include "Constants.h"
#include "World.h"
#include "Funcs.h"
#include "Lighting.h"
#include "Platform.h"
#include "MapRenderer.h"
#include "GraphicsAPI.h"
#include "ErrorHandler.h"
#include "Drawer.h"
#include "ExtMath.h"
#include "ChunkUpdater.h"
#include "BlockID.h"
#include "Block.h"
#include "PackedCol.h"
#include "TerrainAtlas.h"
#include "VertexStructs.h"

BlockID* Builder_Chunk;
UInt8* Builder_Counts;
Int32* Builder_BitFlags;
bool Builder_UseBitFlags;
Int32 Builder_X, Builder_Y, Builder_Z;
BlockID Builder_Block;
Int32 Builder_ChunkIndex;
bool Builder_FullBright;
bool Builder_Tinted;
Int32 Builder_ChunkEndX, Builder_ChunkEndZ;
Int32 Builder_Offsets[FACE_COUNT];

Int32 (*Builder_StretchXLiquid)(Int32 countIndex, Int32 x, Int32 y, Int32 z, Int32 chunkIndex, BlockID block);
Int32 (*Builder_StretchX)(Int32 countIndex, Int32 x, Int32 y, Int32 z, Int32 chunkIndex, BlockID block, Face face);
Int32 (*Builder_StretchZ)(Int32 countIndex, Int32 x, Int32 y, Int32 z, Int32 chunkIndex, BlockID block, Face face);
void (*Builder_RenderBlock)(Int32 countsIndex);
void (*Builder_PreStretchTiles)(Int32 x1, Int32 y1, Int32 z1);
void (*Builder_PostStretchTiles)(Int32 x1, Int32 y1, Int32 z1);

/* Contains state for vertices for a portion of a chunk mesh (vertices that are in a 1D atlas) */
struct Builder1DPart {
	VertexP3fT2fC4b* fVertices[FACE_COUNT];
	Int32 fCount[FACE_COUNT];
	Int32 sCount, sOffset, sAdvance;
};

/* Part builder data, for both normal and translucent parts.
The first ATLAS1D_MAX_ATLASES parts are for normal parts, remainder are for translucent parts. */
struct Builder1DPart Builder_Parts[ATLAS1D_MAX_ATLASES * 2];
VertexP3fT2fC4b* Builder_Vertices;
Int32 Builder_VerticesElems;

static Int32 Builder1DPart_VerticesCount(struct Builder1DPart* part) {
	Int32 i, count = part->sCount;
	for (i = 0; i < FACE_COUNT; i++) { count += part->fCount[i]; }
	return count;
}

static void Builder1DPart_CalcOffsets(struct Builder1DPart* part, Int32* offset) {
	Int32 pos = *offset, i;
	part->sOffset = pos;
	part->sAdvance = part->sCount >> 2;

	pos += part->sCount;
	for (i = 0; i < FACE_COUNT; i++) {
		part->fVertices[i] = &Builder_Vertices[pos];
		pos += part->fCount[i];
	}
	*offset = pos;
}

static Int32 Builder_TotalVerticesCount(void) {
	Int32 i, count = 0;
	for (i = 0; i < ATLAS1D_MAX_ATLASES * 2; i++) {
		count += Builder1DPart_VerticesCount(&Builder_Parts[i]);
	}
	return count;
}


static void Builder_AddSpriteVertices(BlockID block) {
	Int32 i = Atlas1D_Index(Block_GetTexLoc(block, FACE_XMIN));
	struct Builder1DPart* part = &Builder_Parts[i];
	part->sCount += 4 * 4;
}

static void Builder_AddVertices(BlockID block, Face face) {
	Int32 baseOffset = (Block_Draw[block] == DRAW_TRANSLUCENT) * ATLAS1D_MAX_ATLASES;
	Int32 i = Atlas1D_Index(Block_GetTexLoc(block, face));
	struct Builder1DPart* part = &Builder_Parts[baseOffset + i];
	part->fCount[face] += 4;
}

static void Builder_SetPartInfo(struct Builder1DPart* part, Int32* offset, struct ChunkPartInfo* info, bool* hasParts) {
	Int32 vCount = Builder1DPart_VerticesCount(part);
	info->Offset = -1;
	if (!vCount) return;

	info->Offset = *offset;
	*offset += vCount;
	*hasParts = true;

#if CC_BUILD_GL11
	info->Vb = Gfx_CreateVb(&Builder_Vertices[info->Offset], VERTEX_FORMAT_P3FT2FC4B, vCount);
#endif

	info->Counts[FACE_XMIN] = part->fCount[FACE_XMIN];
	info->Counts[FACE_XMAX] = part->fCount[FACE_XMAX];
	info->Counts[FACE_ZMIN] = part->fCount[FACE_ZMIN];
	info->Counts[FACE_ZMAX] = part->fCount[FACE_ZMAX];
	info->Counts[FACE_YMIN] = part->fCount[FACE_YMIN];
	info->Counts[FACE_YMAX] = part->fCount[FACE_YMAX];
	info->SpriteCount       = part->sCount;
}


static void Builder_Stretch(Int32 x1, Int32 y1, Int32 z1) {
	Int32 xMax = min(World_Width,  x1 + CHUNK_SIZE);
	Int32 yMax = min(World_Height, y1 + CHUNK_SIZE);
	Int32 zMax = min(World_Length, z1 + CHUNK_SIZE);
#if OCCLUSION
	Int32 flags = ComputeOcclusion();
#endif
#if DEBUG_OCCLUSION
	FastColour col = new FastColour(60, 60, 60, 255);
	if (flags & 1) col.R = 255; // x
	if (flags & 4) col.G = 255; // y
	if (flags & 2) col.B = 255; // z
	map.Sunlight = map.Shadowlight = col;
	map.SunlightXSide = map.ShadowlightXSide = col;
	map.SunlightZSide = map.ShadowlightZSide = col;
	map.SunlightYBottom = map.ShadowlightYBottom = col;
#endif

	Int32 x, y, z, xx, yy, zz;
	for (y = y1, yy = 0; y < yMax; y++, yy++) {
		for (z = z1, zz = 0; z < zMax; z++, zz++) {
			Int32 cIndex = (yy + 1) * EXTCHUNK_SIZE_2 + (zz + 1) * EXTCHUNK_SIZE + (-1 + 1);
			for (x = x1, xx = 0; x < xMax; x++, xx++) {
				cIndex++;
				BlockID b = Builder_Chunk[cIndex];
				if (Block_Draw[b] == DRAW_GAS) continue;
				Int32 index = ((yy << 8) | (zz << 4) | xx) * FACE_COUNT;

				/* Sprites only use one face to indicate stretching count, so we can take a shortcut here.
				Note that sprites are not drawn with any of the DrawXFace, they are drawn using DrawSprite. */
				if (Block_Draw[b] == DRAW_SPRITE) {
					index += FACE_YMAX;
					if (Builder_Counts[index]) {
						Builder_X = x; Builder_Y = y; Builder_Z = z;
						Builder_AddSpriteVertices(b);
						Builder_Counts[index] = 1;
					}
					continue;
				}

				Builder_X = x; Builder_Y = y; Builder_Z = z;
				Builder_FullBright = Block_FullBright[b];
				UInt32 tileIdx = b * BLOCK_COUNT;
				/* All of these function calls are inlined as they can be called tens of millions to hundreds of millions of times. */

				if (Builder_Counts[index] == 0 ||
					(x == 0 && (y < Builder_SidesLevel || (b >= BLOCK_WATER && b <= BLOCK_STILL_LAVA && y < Builder_EdgeLevel))) ||
					(x != 0 && (Block_Hidden[tileIdx + Builder_Chunk[cIndex - 1]] & (1 << FACE_XMIN)) != 0)) {
					Builder_Counts[index] = 0;
				} else {
					Int32 count = Builder_StretchZ(index, x, y, z, cIndex, b, FACE_XMIN);
					Builder_AddVertices(b, FACE_XMIN);
					Builder_Counts[index] = (UInt8)count;
				}

				index++;
				if (Builder_Counts[index] == 0 ||
					(x == World_MaxX && (y < Builder_SidesLevel || (b >= BLOCK_WATER && b <= BLOCK_STILL_LAVA && y < Builder_EdgeLevel))) ||
					(x != World_MaxX && (Block_Hidden[tileIdx + Builder_Chunk[cIndex + 1]] & (1 << FACE_XMAX)) != 0)) {
					Builder_Counts[index] = 0;
				} else {
					Int32 count = Builder_StretchZ(index, x, y, z, cIndex, b, FACE_XMAX);
					Builder_AddVertices(b, FACE_XMAX);
					Builder_Counts[index] = (UInt8)count;
				}

				index++;
				if (Builder_Counts[index] == 0 ||
					(z == 0 && (y < Builder_SidesLevel || (b >= BLOCK_WATER && b <= BLOCK_STILL_LAVA && y < Builder_EdgeLevel))) ||
					(z != 0 && (Block_Hidden[tileIdx + Builder_Chunk[cIndex - EXTCHUNK_SIZE]] & (1 << FACE_ZMIN)) != 0)) {
					Builder_Counts[index] = 0;
				} else {
					Int32 count = Builder_StretchX(index, Builder_X, Builder_Y, Builder_Z, cIndex, b, FACE_ZMIN);
					Builder_AddVertices(b, FACE_ZMIN);
					Builder_Counts[index] = (UInt8)count;
				}

				index++;
				if (Builder_Counts[index] == 0 ||
					(z == World_MaxZ && (y < Builder_SidesLevel || (b >= BLOCK_WATER && b <= BLOCK_STILL_LAVA && y < Builder_EdgeLevel))) ||
					(z != World_MaxZ && (Block_Hidden[tileIdx + Builder_Chunk[cIndex + EXTCHUNK_SIZE]] & (1 << FACE_ZMAX)) != 0)) {
					Builder_Counts[index] = 0;
				} else {
					Int32 count = Builder_StretchX(index, x, y, z, cIndex, b, FACE_ZMAX);
					Builder_AddVertices(b, FACE_ZMAX);
					Builder_Counts[index] = (UInt8)count;
				}

				index++;
				if (Builder_Counts[index] == 0 || y == 0 ||
					(Block_Hidden[tileIdx + Builder_Chunk[cIndex - EXTCHUNK_SIZE_2]] & (1 << FACE_YMIN)) != 0) {
					Builder_Counts[index] = 0;
				} else {
					Int32 count = Builder_StretchX(index, x, y, z, cIndex, b, FACE_YMIN);
					Builder_AddVertices(b, FACE_YMIN);
					Builder_Counts[index] = (UInt8)count;
				}

				index++;
				if (Builder_Counts[index] == 0 ||
					(Block_Hidden[tileIdx + Builder_Chunk[cIndex + EXTCHUNK_SIZE_2]] & (1 << FACE_YMAX)) != 0) {
					Builder_Counts[index] = 0;
				} else if (b < BLOCK_WATER || b > BLOCK_STILL_LAVA) {
					Int32 count = Builder_StretchX(index, x, y, z, cIndex, b, FACE_YMAX);
					Builder_AddVertices(b, FACE_YMAX);
					Builder_Counts[index] = (UInt8)count;
				} else {
					Int32 count = Builder_StretchXLiquid(index, x, y, z, cIndex, b);
					if (count > 0) Builder_AddVertices(b, FACE_YMAX);
					Builder_Counts[index] = (UInt8)count;
				}
			}
		}
	}
}

static void Builder_ReadChunkData(Int32 x1, Int32 y1, Int32 z1, bool* outAllAir, bool* outAllSolid) {
	bool allAir = true, allSolid = true;
	Int32 xx, yy, zz;

	for (yy = -1; yy < 17; ++yy) {
		Int32 y = yy + y1;
		if (y < 0) continue;
		if (y >= World_Height) break;

		for (zz = -1; zz < 17; ++zz) {
			Int32 z = zz + z1;
			if (z < 0) continue;
			if (z >= World_Length) break;

			/* need to subtract 1 as index is pre incremented in for loop. */
			Int32 index = World_Pack(x1 - 1, y, z) - 1;
			Int32 chunkIndex = (yy + 1) * EXTCHUNK_SIZE_2 + (zz + 1) * EXTCHUNK_SIZE + (-1 + 1) - 1;

			for (xx = -1; xx < 17; ++xx) {
				Int32 x = xx + x1;
				++index;
				++chunkIndex;

				if (x < 0) continue;
				if (x >= World_Width) break;
				BlockID rawBlock = World_Blocks[index];

				allAir = allAir && Block_Draw[rawBlock] == DRAW_GAS;
				allSolid = allSolid && Block_FullOpaque[rawBlock];
				Builder_Chunk[chunkIndex] = rawBlock;
			}
		}
	}

	*outAllAir = allAir;
	*outAllSolid = allSolid;
}

static bool Builder_BuildChunk(Int32 x1, Int32 y1, Int32 z1, bool* allAir) {
	Builder_PreStretchTiles(x1, y1, z1);
	BlockID chunk[EXTCHUNK_SIZE_3]; Builder_Chunk = chunk;
	UInt8 counts[CHUNK_SIZE_3 * FACE_COUNT]; Builder_Counts = counts;
	Int32 bitFlags[EXTCHUNK_SIZE_3]; Builder_BitFlags = bitFlags;

	Mem_Set(chunk, BLOCK_AIR, EXTCHUNK_SIZE_3 * sizeof(BlockID));
	bool allSolid;
	Builder_ReadChunkData(x1, y1, z1, allAir, &allSolid);

	if (x1 == 0 || y1 == 0 || z1 == 0 || x1 + CHUNK_SIZE >= World_Width ||
		y1 + CHUNK_SIZE >= World_Height || z1 + CHUNK_SIZE >= World_Length) allSolid = false;

	if (*allAir || allSolid) return false;
	Lighting_LightHint(x1 - 1, z1 - 1);

	Mem_Set(counts, 1, CHUNK_SIZE_3 * FACE_COUNT);
	Int32 xMax = min(World_Width, x1 + CHUNK_SIZE);
	Int32 yMax = min(World_Height, y1 + CHUNK_SIZE);
	Int32 zMax = min(World_Length, z1 + CHUNK_SIZE);

	Builder_ChunkEndX = xMax; Builder_ChunkEndZ = zMax;
	Builder_Stretch(x1, y1, z1);
	Builder_PostStretchTiles(x1, y1, z1);
	Int32 x, y, z, xx, yy, zz;

	for (y = y1, yy = 0; y < yMax; y++, yy++) {
		for (z = z1, zz = 0; z < zMax; z++, zz++) {

			Int32 chunkIndex = (yy + 1) * EXTCHUNK_SIZE_2 + (zz + 1) * EXTCHUNK_SIZE + (0 + 1);
			for (x = x1, xx = 0; x < xMax; x++, xx++) {
				Builder_Block = chunk[chunkIndex];
				if (Block_Draw[Builder_Block] != DRAW_GAS) {
					Int32 index = ((yy << 8) | (zz << 4) | xx) * FACE_COUNT;
					Builder_X = x; Builder_Y = y; Builder_Z = z;
					Builder_ChunkIndex = chunkIndex;
					Builder_RenderBlock(index);
				}
				chunkIndex++;
			}
		}
	}
	return true;
}

void Builder_MakeChunk(struct ChunkInfo* info) {
	Int32 x = info->CentreX - 8, y = info->CentreY - 8, z = info->CentreZ - 8;
	bool allAir = false, hasMesh;
	hasMesh = Builder_BuildChunk(x, y, z, &allAir);
	info->AllAir = allAir;
	if (!hasMesh) return;

	Int32 totalVerts = Builder_TotalVerticesCount();
	if (!totalVerts) return;
#if !CC_BUILD_GL11
	/* add an extra element to fix crashing on some GPUs */
	info->Vb = Gfx_CreateVb(Builder_Vertices, VERTEX_FORMAT_P3FT2FC4B, totalVerts + 1);
#endif

	Int32 i, offset = 0, partsIndex = MapRenderer_Pack(x >> CHUNK_SHIFT, y >> CHUNK_SHIFT, z >> CHUNK_SHIFT);
	bool hasNormal = false, hasTranslucent = false;

	for (i = 0; i < MapRenderer_1DUsedCount; i++) {
		Int32 j = i + ATLAS1D_MAX_ATLASES;
		Int32 curIdx = partsIndex + i * MapRenderer_ChunksCount;

		Builder_SetPartInfo(&Builder_Parts[i], &offset, &MapRenderer_PartsNormal[curIdx],      &hasNormal);
		Builder_SetPartInfo(&Builder_Parts[j], &offset, &MapRenderer_PartsTranslucent[curIdx], &hasTranslucent);
	}

	if (hasNormal) {
		info->NormalParts = &MapRenderer_PartsNormal[partsIndex];
	}
	if (hasTranslucent) {
		info->TranslucentParts = &MapRenderer_PartsTranslucent[partsIndex];
	}

#if OCCLUSION
	if (info.NormalParts != null || info.TranslucentParts != null)
		info.occlusionFlags = (UInt8)ComputeOcclusion();
#endif
}

static bool Builder_OccludedLiquid(Int32 chunkIndex) {
	chunkIndex += EXTCHUNK_SIZE_2; /* Checking y above */
	return
		Block_FullOpaque[Builder_Chunk[chunkIndex]]
		&& Block_Draw[Builder_Chunk[chunkIndex - EXTCHUNK_SIZE]] != DRAW_GAS
		&& Block_Draw[Builder_Chunk[chunkIndex - 1]] != DRAW_GAS
		&& Block_Draw[Builder_Chunk[chunkIndex + 1]] != DRAW_GAS
		&& Block_Draw[Builder_Chunk[chunkIndex + EXTCHUNK_SIZE]] != DRAW_GAS;
}

static void Builder_DefaultPreStretchTiles(Int32 x1, Int32 y1, Int32 z1) {
	Mem_Set(Builder_Parts, 0, sizeof(Builder_Parts));
}

static void Builder_DefaultPostStretchTiles(Int32 x1, Int32 y1, Int32 z1) {
	Int32 i, vertsCount = Builder_TotalVerticesCount();
	if (vertsCount > Builder_VerticesElems) {
		Mem_Free(&Builder_Vertices);
		/* ensure buffer can be accessed with 64 bytes alignment by putting 2 extra vertices at end. */
		Builder_Vertices = Mem_Alloc(vertsCount + 2, sizeof(VertexP3fT2fC4b), "chunk vertices");
		Builder_VerticesElems = vertsCount;
	}

	vertsCount = 0;
	for (i = 0; i < ATLAS1D_MAX_ATLASES; i++) {
		Int32 j = i + ATLAS1D_MAX_ATLASES;
		Builder1DPart_CalcOffsets(&Builder_Parts[i], &vertsCount);
		Builder1DPart_CalcOffsets(&Builder_Parts[j], &vertsCount);
	}
}

Random spriteRng;
static void Builder_DrawSprite(Int32 count) {
	TextureLoc texLoc = Block_GetTexLoc(Builder_Block, FACE_XMAX);
	Int32 i = Atlas1D_Index(texLoc);
	Real32 vOrigin = Atlas1D_RowId(texLoc) * Atlas1D_InvTileSize;
	Real32 X = (Real32)Builder_X, Y = (Real32)Builder_Y, Z = (Real32)Builder_Z;

#define u1 0.0f
#define u2 UV2_Scale
	Real32 x1 = (Real32)X + 2.50f / 16.0f, y1 = (Real32)Y,        z1 = (Real32)Z + 2.50f / 16.0f;
	Real32 x2 = (Real32)X + 13.5f / 16.0f, y2 = (Real32)Y + 1.0f, z2 = (Real32)Z + 13.5f / 16.0f;
	Real32 v1 = vOrigin, v2 = vOrigin + Atlas1D_InvTileSize * UV2_Scale;

	UInt8 offsetType = Block_SpriteOffset[Builder_Block];
	if (offsetType >= 6 && offsetType <= 7) {
		Random_SetSeed(&spriteRng, (Builder_X + 1217 * Builder_Z) & 0x7fffffff);
		Real32 valX = Random_Range(&spriteRng, -3, 3 + 1) / 16.0f;
		Real32 valY = Random_Range(&spriteRng, 0,  3 + 1) / 16.0f;
		Real32 valZ = Random_Range(&spriteRng, -3, 3 + 1) / 16.0f;

#define stretch 1.7f / 16.0f
		x1 += valX - stretch; x2 += valX + stretch;
		z1 += valZ - stretch; z2 += valZ + stretch;
		if (offsetType == 7) { y1 -= valY; y2 -= valY; }
	}
	
	struct Builder1DPart* part = &Builder_Parts[i];
	PackedCol white = PACKEDCOL_WHITE;
	PackedCol col = Builder_FullBright ? white : Lighting_Col_Sprite_Fast(Builder_X, Builder_Y, Builder_Z);
	Block_Tint(col, Builder_Block);
	VertexP3fT2fC4b v; v.Col = col;

	/* Draw Z axis */
	Int32 index = part->sOffset;
	v.X = x1; v.Y = y1; v.Z = z1; v.U = u2; v.V = v2; Builder_Vertices[index + 0] = v;
	          v.Y = y2;                     v.V = v1; Builder_Vertices[index + 1] = v;
	v.X = x2;           v.Z = z2; v.U = u1;           Builder_Vertices[index + 2] = v;
	          v.Y = y1;                     v.V = v2; Builder_Vertices[index + 3] = v;

	/* Draw Z axis mirrored */
	index += part->sAdvance;
	v.X = x2; v.Y = y1; v.Z = z2; v.U = u2;           Builder_Vertices[index + 0] = v;
	          v.Y = y2;                     v.V = v1; Builder_Vertices[index + 1] = v;
	v.X = x1;           v.Z = z1; v.U = u1;           Builder_Vertices[index + 2] = v;
	          v.Y = y1;                     v.V = v2; Builder_Vertices[index + 3] = v;

	/* Draw X axis */
	index += part->sAdvance;
	v.X = x1; v.Y = y1; v.Z = z2; v.U = u2;           Builder_Vertices[index + 0] = v;
	          v.Y = y2;                     v.V = v1; Builder_Vertices[index + 1] = v;
	v.X = x2;           v.Z = z1; v.U = u1;           Builder_Vertices[index + 2] = v;
	          v.Y = y1;                     v.V = v2; Builder_Vertices[index + 3] = v;

	/* Draw X axis mirrored */
	index += part->sAdvance;
	v.X = x2; v.Y = y1; v.Z = z1; v.U = u2;           Builder_Vertices[index + 0] = v;
	          v.Y = y2;                     v.V = v1; Builder_Vertices[index + 1] = v;
	v.X = x1;           v.Z = z2; v.U = u1;           Builder_Vertices[index + 2] = v;
	          v.Y = y1;                     v.V = v2; Builder_Vertices[index + 3] = v;

	part->sOffset += 4;
}

void Builder_Init(void) {
	Builder_Offsets[FACE_XMIN] = -1;
	Builder_Offsets[FACE_XMAX] = 1;
	Builder_Offsets[FACE_ZMIN] = -EXTCHUNK_SIZE;
	Builder_Offsets[FACE_ZMAX] = EXTCHUNK_SIZE;
	Builder_Offsets[FACE_YMIN] = -EXTCHUNK_SIZE_2;
	Builder_Offsets[FACE_YMAX] = EXTCHUNK_SIZE_2;
}

void Builder_OnNewMapLoaded(void) {
	Builder_SidesLevel = max(0, WorldEnv_SidesHeight);
	Builder_EdgeLevel  = max(0, WorldEnv_EdgeHeight);
}


static PackedCol NormalBuilder_LightCol(Int32 x, Int32 y, Int32 z, Int32 face, BlockID block) {
	Int32 offset = (Block_LightOffset[block] >> face) & 1;
	switch (face) {
	case FACE_XMIN:
		return x < offset ? Lighting_OutsideXSide : Lighting_Col_XSide_Fast(x - offset, y, z);
	case FACE_XMAX:
		return x >(World_MaxX - offset) ? Lighting_OutsideXSide : Lighting_Col_XSide_Fast(x + offset, y, z);
	case FACE_ZMIN:
		return z < offset ? Lighting_OutsideZSide : Lighting_Col_ZSide_Fast(x, y, z - offset);
	case FACE_ZMAX:
		return z >(World_MaxZ - offset) ? Lighting_OutsideZSide : Lighting_Col_ZSide_Fast(x, y, z + offset);
	case FACE_YMIN:
		return y <= 0 ? Lighting_OutsideYBottom : Lighting_Col_YBottom_Fast(x, y - offset, z);
	case FACE_YMAX:
		return y >= World_MaxY ? Lighting_Outside : Lighting_Col_YTop_Fast(x, (y + 1) - offset, z);
	}

	PackedCol black = PACKEDCOL_BLACK;
	return black;
}

static bool NormalBuilder_CanStretch(BlockID initial, Int32 chunkIndex, Int32 x, Int32 y, Int32 z, Face face) {
	BlockID cur = Builder_Chunk[chunkIndex];
	return cur == initial
		&& !Block_IsFaceHidden(cur, Builder_Chunk[chunkIndex + Builder_Offsets[face]], face)
		&& (Builder_FullBright || (NormalBuilder_LightCol(Builder_X, Builder_Y, Builder_Z, face, initial).Packed == NormalBuilder_LightCol(x, y, z, face, cur).Packed));
}

static Int32 NormalBuilder_StretchXLiquid(Int32 countIndex, Int32 x, Int32 y, Int32 z, Int32 chunkIndex, BlockID block) {
	if (Builder_OccludedLiquid(chunkIndex)) return 0;
	Int32 count = 1;
	x++;
	chunkIndex++;
	countIndex += FACE_COUNT;
	bool stretchTile = (Block_CanStretch[block] & (1 << FACE_YMAX)) != 0;

	while (x < Builder_ChunkEndX && stretchTile && NormalBuilder_CanStretch(block, chunkIndex, x, y, z, FACE_YMAX) && !Builder_OccludedLiquid(chunkIndex)) {
		Builder_Counts[countIndex] = 0;
		count++;
		x++;
		chunkIndex++;
		countIndex += FACE_COUNT;
	}
	return count;
}

static Int32 NormalBuilder_StretchX(Int32 countIndex, Int32 x, Int32 y, Int32 z, Int32 chunkIndex, BlockID block, Face face) {
	Int32 count = 1;
	x++;
	chunkIndex++;
	countIndex += FACE_COUNT;
	bool stretchTile = (Block_CanStretch[block] & (1 << face)) != 0;

	while (x < Builder_ChunkEndX && stretchTile && NormalBuilder_CanStretch(block, chunkIndex, x, y, z, face)) {
		Builder_Counts[countIndex] = 0;
		count++;
		x++;
		chunkIndex++;
		countIndex += FACE_COUNT;
	}
	return count;
}

static Int32 NormalBuilder_StretchZ(Int32 countIndex, Int32 x, Int32 y, Int32 z, Int32 chunkIndex, BlockID block, Face face) {
	Int32 count = 1;
	z++;
	chunkIndex += EXTCHUNK_SIZE;
	countIndex += CHUNK_SIZE * FACE_COUNT;
	bool stretchTile = (Block_CanStretch[block] & (1 << face)) != 0;

	while (z < Builder_ChunkEndZ && stretchTile && NormalBuilder_CanStretch(block, chunkIndex, x, y, z, face)) {
		Builder_Counts[countIndex] = 0;
		count++;
		z++;
		chunkIndex += EXTCHUNK_SIZE;
		countIndex += CHUNK_SIZE * FACE_COUNT;
	}
	return count;
}

static void NormalBuilder_RenderBlock(Int32 index) {
	if (Block_Draw[Builder_Block] == DRAW_SPRITE) {
		Builder_FullBright = Block_FullBright[Builder_Block];
		Builder_Tinted = Block_Tinted[Builder_Block];

		Int32 count = Builder_Counts[index + FACE_YMAX];
		if (count) Builder_DrawSprite(count);
		return;
	}

	Int32 count_XMin = Builder_Counts[index + FACE_XMIN];
	Int32 count_XMax = Builder_Counts[index + FACE_XMAX];
	Int32 count_ZMin = Builder_Counts[index + FACE_ZMIN];
	Int32 count_ZMax = Builder_Counts[index + FACE_ZMAX];
	Int32 count_YMin = Builder_Counts[index + FACE_YMIN];
	Int32 count_YMax = Builder_Counts[index + FACE_YMAX];

	if (count_XMin == 0 && count_XMax == 0 && count_ZMin == 0 &&
		count_ZMax == 0 && count_YMin == 0 && count_YMax == 0) return;


	bool fullBright = Block_FullBright[Builder_Block];
	Int32 partOffset = (Block_Draw[Builder_Block] == DRAW_TRANSLUCENT) * ATLAS1D_MAX_ATLASES;
	Int32 lightFlags = Block_LightOffset[Builder_Block];

	Drawer_MinBB = Block_MinBB[Builder_Block]; Drawer_MinBB.Y = 1.0f - Drawer_MinBB.Y;
	Drawer_MaxBB = Block_MaxBB[Builder_Block]; Drawer_MaxBB.Y = 1.0f - Drawer_MaxBB.Y;

	Vector3 min = Block_RenderMinBB[Builder_Block], max = Block_RenderMaxBB[Builder_Block];
	Drawer_X1 = Builder_X + min.X; Drawer_Y1 = Builder_Y + min.Y; Drawer_Z1 = Builder_Z + min.Z;
	Drawer_X2 = Builder_X + max.X; Drawer_Y2 = Builder_Y + max.Y; Drawer_Z2 = Builder_Z + max.Z;

	Drawer_Tinted = Block_Tinted[Builder_Block];
	Drawer_TintColour = Block_FogCol[Builder_Block];
	PackedCol white = PACKEDCOL_WHITE;

	if (count_XMin) {
		TextureLoc texLoc = Block_GetTexLoc(Builder_Block, FACE_XMIN);
		Int32 offset = (lightFlags >> FACE_XMIN) & 1;
		struct Builder1DPart* part = &Builder_Parts[partOffset + Atlas1D_Index(texLoc)];

		PackedCol col = fullBright ? white :
			Builder_X >= offset ? Lighting_Col_XSide_Fast(Builder_X - offset, Builder_Y, Builder_Z) : Lighting_OutsideXSide;
		Drawer_XMin(count_XMin, col, texLoc, &part->fVertices[FACE_XMIN]);
	}

	if (count_XMax) {
		TextureLoc texLoc = Block_GetTexLoc(Builder_Block, FACE_XMAX);
		Int32 offset = (lightFlags >> FACE_XMAX) & 1;
		struct Builder1DPart* part = &Builder_Parts[partOffset + Atlas1D_Index(texLoc)];

		PackedCol col = fullBright ? white :
			Builder_X <= (World_MaxX - offset) ? Lighting_Col_XSide_Fast(Builder_X + offset, Builder_Y, Builder_Z) : Lighting_OutsideXSide;
		Drawer_XMax(count_XMax, col, texLoc, &part->fVertices[FACE_XMAX]);
	}

	if (count_ZMin) {
		TextureLoc texLoc = Block_GetTexLoc(Builder_Block, FACE_ZMIN);
		Int32 offset = (lightFlags >> FACE_ZMIN) & 1;
		struct Builder1DPart* part = &Builder_Parts[partOffset + Atlas1D_Index(texLoc)];

		PackedCol col = fullBright ? white :
			Builder_Z >= offset ? Lighting_Col_ZSide_Fast(Builder_X, Builder_Y, Builder_Z - offset) : Lighting_OutsideZSide;
		Drawer_ZMin(count_ZMin, col, texLoc, &part->fVertices[FACE_ZMIN]);
	}

	if (count_ZMax) {
		TextureLoc texLoc = Block_GetTexLoc(Builder_Block, FACE_ZMAX);
		Int32 offset = (lightFlags >> FACE_ZMAX) & 1;
		struct Builder1DPart* part = &Builder_Parts[partOffset + Atlas1D_Index(texLoc)];

		PackedCol col = fullBright ? white :
			Builder_Z <= (World_MaxZ - offset) ? Lighting_Col_ZSide_Fast(Builder_X, Builder_Y, Builder_Z + offset) : Lighting_OutsideZSide;
		Drawer_ZMax(count_ZMax, col, texLoc, &part->fVertices[FACE_ZMAX]);
	}

	if (count_YMin) {
		TextureLoc texLoc = Block_GetTexLoc(Builder_Block, FACE_YMIN);
		Int32 offset = (lightFlags >> FACE_YMIN) & 1;
		struct Builder1DPart* part = &Builder_Parts[partOffset + Atlas1D_Index(texLoc)];

		PackedCol col = fullBright ? white : Lighting_Col_YBottom_Fast(Builder_X, Builder_Y - offset, Builder_Z);
		Drawer_YMin(count_YMin, col, texLoc, &part->fVertices[FACE_YMIN]);
	}

	if (count_YMax) {
		TextureLoc texLoc = Block_GetTexLoc(Builder_Block, FACE_YMAX);
		Int32 offset = (lightFlags >> FACE_YMAX) & 1;
		struct Builder1DPart* part = &Builder_Parts[partOffset + Atlas1D_Index(texLoc)];

		PackedCol col = fullBright ? white : Lighting_Col_YTop_Fast(Builder_X, (Builder_Y + 1) - offset, Builder_Z);
		Drawer_YMax(count_YMax, col, texLoc, &part->fVertices[FACE_YMAX]);
	}
}

static void Builder_SetDefault(void) {
	Builder_StretchXLiquid = NULL;
	Builder_StretchX       = NULL;
	Builder_StretchZ       = NULL;
	Builder_RenderBlock    = NULL;

	Builder_UseBitFlags      = false;
	Builder_PreStretchTiles  = Builder_DefaultPreStretchTiles;
	Builder_PostStretchTiles = Builder_DefaultPostStretchTiles;
}

void NormalBuilder_SetActive(void) {
	Builder_SetDefault();
	Builder_StretchXLiquid = NormalBuilder_StretchXLiquid;
	Builder_StretchX       = NormalBuilder_StretchX;
	Builder_StretchZ       = NormalBuilder_StretchZ;
	Builder_RenderBlock    = NormalBuilder_RenderBlock;
}
