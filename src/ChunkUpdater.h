#ifndef CC_CHUNKUPDATER_H
#define CC_CHUNKUPDATER_H
#include "Core.h"
#include "Constants.h"
/* Manages the process of building/deleting chunk meshes.
   Also sorts chunks so nearest chunks are ordered first, and calculates chunk visibility.
   Copyright 2014-2017 ClassicalSharp | Licensed under BSD-3
*/

/* Describes a portion of the data needed for rendering a chunk. */
struct ChunkPartInfo {
#if CC_BUILD_GL11
	GfxResourceID Vb;
#endif
	Int32 Offset;              /* -1 if no vertices at all */
	Int32 SpriteCount;         /* Sprite vertices count */
	UInt16 Counts[FACE_COUNT]; /* Counts per face */
};

/* Describes data necessary for rendering a chunk. */
struct ChunkInfo {	
	UInt16 CentreX, CentreY, CentreZ; /* Centre coordinates of the chunk */

	UInt8 Visible : 1;       /* Whether chunk is visibile to the player */
	UInt8 Empty : 1;         /* Whether the chunk is empty of data */
	UInt8 PendingDelete : 1; /* Whether chunk is pending deletion*/	
	UInt8 AllAir : 1;        /* Whether chunk is completely air */
	UInt8 : 0;               /* pad to next byte*/

	UInt8 DrawXMin : 1;
	UInt8 DrawXMax : 1;
	UInt8 DrawZMin : 1;
	UInt8 DrawZMax : 1;
	UInt8 DrawYMin : 1;
	UInt8 DrawYMax : 1;
	UInt8 : 0;          /* pad to next byte */
#if OCCLUSION
	public bool Visited = false, Occluded = false;
	public byte OcclusionFlags, OccludedFlags, DistanceFlags;
#endif
#if !CC_BUILD_GL11
	GfxResourceID Vb;
#endif
	struct ChunkPartInfo* NormalParts;
	struct ChunkPartInfo* TranslucentParts;
};

void ChunkInfo_Reset(struct ChunkInfo* chunk, Int32 x, Int32 y, Int32 z);

void ChunkUpdater_Init(void);
void ChunkUpdater_Free(void);
void ChunkUpdater_Refresh(void);
void ChunkUpdater_RefreshBorders(Int32 clipLevel);
void ChunkUpdater_ApplyMeshBuilder(void);
void ChunkUpdater_Update(Real64 deltaTime);

void ChunkUpdater_ResetPartFlags(void);
void ChunkUpdater_ResetPartCounts(void);
void ChunkUpdater_CreateChunkCache(void);
void ChunkUpdater_ResetChunkCache(void);
void ChunkUpdater_ClearChunkCache(void);

void ChunkUpdater_DeleteChunk(struct ChunkInfo* info);
void ChunkUpdater_BuildChunk(struct ChunkInfo* info, Int32* chunkUpdates);
#endif
