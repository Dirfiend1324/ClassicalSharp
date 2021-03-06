#include "Picking.h"
#include "ExtMath.h"
#include "Game.h"
#include "Physics.h"
#include "Entity.h"
#include "World.h"
#include "Funcs.h"
#include "BlockID.h"
#include "Block.h"
#include "ErrorHandler.h"

Real32 PickedPos_dist;
static void PickedPos_TestAxis(struct PickedPos* pos, Real32 dAxis, Face fAxis) {
	dAxis = Math_AbsF(dAxis);
	if (dAxis >= PickedPos_dist) return;

	PickedPos_dist = dAxis;
	pos->ClosestFace = fAxis;
}

void PickedPos_SetAsValid(struct PickedPos* pos, struct RayTracer* t, Vector3 intersect) {
	Vector3I blockPos  = { t->X, t->Y, t->Z };
	pos->Valid         = true;
	pos->BlockPos      = blockPos;
	pos->Block         = t->Block;	
	pos->Intersect     = intersect;
	pos->Min = t->Min; pos->Max = t->Max;

	PickedPos_dist = MATH_LARGENUM;
	PickedPos_TestAxis(pos, intersect.X - t->Min.X, FACE_XMIN);
	PickedPos_TestAxis(pos, intersect.X - t->Max.X, FACE_XMAX);
	PickedPos_TestAxis(pos, intersect.Y - t->Min.Y, FACE_YMIN);
	PickedPos_TestAxis(pos, intersect.Y - t->Max.Y, FACE_YMAX);
	PickedPos_TestAxis(pos, intersect.Z - t->Min.Z, FACE_ZMIN);
	PickedPos_TestAxis(pos, intersect.Z - t->Max.Z, FACE_ZMAX);

	switch (pos->ClosestFace) {
	case FACE_XMIN: blockPos.X--; break;
	case FACE_XMAX: blockPos.X++; break;
	case FACE_ZMIN: blockPos.Z--; break;
	case FACE_ZMAX: blockPos.Z++; break;
	case FACE_YMIN: blockPos.Y--; break;
	case FACE_YMAX: blockPos.Y++; break;
	}
	pos->TranslatedPos = blockPos;
}

void PickedPos_SetAsInvalid(struct PickedPos* pos) {
	Vector3I blockPos  = { -1, -1, -1 };
	pos->Valid         = false;
	pos->BlockPos      = blockPos;
	pos->Block         = BLOCK_AIR;
	pos->ClosestFace   = (Face)FACE_COUNT;
	pos->TranslatedPos = blockPos;
}

static Real32 RayTracer_Div(Real32 a, Real32 b) {
	if (Math_AbsF(b) < 0.000001f) return MATH_LARGENUM;
	return a / b;
}

void RayTracer_SetVectors(struct RayTracer* t, Vector3 origin, Vector3 dir) {
	t->Origin = origin; t->Dir = dir;
	/* Rounds the position's X, Y and Z down to the nearest integer values. */
	Vector3I start;
	Vector3I_Floor(&start, &origin);
	/* The cell in which the ray starts. */
	t->X = start.X; t->Y = start.Y; t->Z = start.Z;
	/* Determine which way we go.*/
	t->step.X = Math_Sign(dir.X); t->step.Y = Math_Sign(dir.Y); t->step.Z = Math_Sign(dir.Z);

	/* Calculate cell boundaries. When the step (i.e. direction sign) is positive,
	the next boundary is AFTER our current position, meaning that we have to add 1.
	Otherwise, it is BEFORE our current position, in which case we add nothing. */
	Vector3I cellBoundary;
	cellBoundary.X = start.X + (t->step.X > 0 ? 1 : 0);
	cellBoundary.Y = start.Y + (t->step.Y > 0 ? 1 : 0);
	cellBoundary.Z = start.Z + (t->step.Z > 0 ? 1 : 0);

	/* NOTE: we want it so if dir.x = 0, tmax.x = positive infinity
	Determine how far we can travel along the ray before we hit a voxel boundary. */
	t->tMax.X = RayTracer_Div(cellBoundary.X - origin.X, dir.X); /* Boundary is a plane on the YZ axis. */
	t->tMax.Y = RayTracer_Div(cellBoundary.Y - origin.Y, dir.Y); /* Boundary is a plane on the XZ axis. */
	t->tMax.Z = RayTracer_Div(cellBoundary.Z - origin.Z, dir.Z); /* Boundary is a plane on the XY axis. */

	/* Determine how far we must travel along the ray before we have crossed a gridcell. */
	t->tDelta.X = RayTracer_Div((Real32)t->step.X, dir.X);
	t->tDelta.Y = RayTracer_Div((Real32)t->step.Y, dir.Y);
	t->tDelta.Z = RayTracer_Div((Real32)t->step.Z, dir.Z);
}

void RayTracer_Step(struct RayTracer* t) {
	/* For each step, determine which distance to the next voxel boundary is lowest
	(i.e. which voxel boundary is nearest) and walk that way. */
	if (t->tMax.X < t->tMax.Y && t->tMax.X < t->tMax.Z) {
		/* tMax.X is the lowest, an YZ cell boundary plane is nearest. */
		t->X += t->step.X;
		t->tMax.X += t->tDelta.X;
	} else if (t->tMax.Y < t->tMax.Z) {
		/* tMax.Y is the lowest, an XZ cell boundary plane is nearest. */
		t->Y += t->step.Y;
		t->tMax.Y += t->tDelta.Y;
	} else {
		/* tMax.Z is the lowest, an XY cell boundary plane is nearest. */
		t->Z += t->step.Z;
		t->tMax.Z += t->tDelta.Z;
	}
}

struct RayTracer tracer;
#define PICKING_BORDER BLOCK_BEDROCK
typedef bool(*IntersectTest)(struct PickedPos* pos);

static BlockID Picking_InsideGetBlock(Int32 x, Int32 y, Int32 z) {
	if (x >= 0 && z >= 0 && x < World_Width && z < World_Length) {
		if (y >= World_Height) return BLOCK_AIR;
		if (y >= 0) return World_GetBlock(x, y, z);
	}

	/* bedrock on bottom or outside map */
	bool sides = WorldEnv_SidesBlock != BLOCK_AIR;
	Int32 height = WorldEnv_SidesHeight; if (height < 1) height = 1;
	return sides && y < height ? PICKING_BORDER : BLOCK_AIR;
}

static BlockID Picking_OutsideGetBlock(Int32 x, Int32 y, Int32 z, Vector3I origin) {
	if (x < 0 || z < 0 || x >= World_Width || z >= World_Length) return BLOCK_AIR;
	bool sides = WorldEnv_SidesBlock != BLOCK_AIR;
	/* handling of blocks inside the map, above, and on borders */

	if (y >= World_Height) return BLOCK_AIR;
	if (sides && y == -1 && origin.Y > 0) return PICKING_BORDER;
	if (sides && y ==  0 && origin.Y < 0) return PICKING_BORDER;
	Int32 height = WorldEnv_SidesHeight; if (height < 1) height = 1;

	if (sides && x == 0          && y >= 0 && y < height && origin.X < 0)             return PICKING_BORDER;
	if (sides && z == 0          && y >= 0 && y < height && origin.Z < 0)             return PICKING_BORDER;
	if (sides && x == World_MaxX && y >= 0 && y < height && origin.X >= World_Width)  return PICKING_BORDER;
	if (sides && z == World_MaxZ && y >= 0 && y < height && origin.Z >= World_Length) return PICKING_BORDER;
	if (y >= 0) return World_GetBlock(x, y, z);
	return BLOCK_AIR;
}

static bool Picking_RayTrace(Vector3 origin, Vector3 dir, Real32 reach, struct PickedPos* pos, IntersectTest intersect) {
	RayTracer_SetVectors(&tracer, origin, dir);
	Real32 reachSq = reach * reach;
	Vector3I pOrigin; Vector3I_Floor(&pOrigin, &origin);
	bool insideMap = World_IsValidPos_3I(pOrigin);

	Int32 i;
	Vector3 coords;
	for (i = 0; i < 10000; i++) {
		Int32 x = tracer.X, y = tracer.Y, z = tracer.Z;
		coords.X = (Real32)x; coords.Y = (Real32)y; coords.Z = (Real32)z;
		tracer.Block = insideMap ?
			Picking_InsideGetBlock(x, y, z) : Picking_OutsideGetBlock(x, y, z, pOrigin);

		Vector3 minPos, maxPos;
		Vector3_Add(&minPos, &coords, &Block_RenderMinBB[tracer.Block]);
		Vector3_Add(&maxPos, &coords, &Block_RenderMaxBB[tracer.Block]);

		Real32 dxMin = Math_AbsF(origin.X - minPos.X), dxMax = Math_AbsF(origin.X - maxPos.X);
		Real32 dyMin = Math_AbsF(origin.Y - minPos.Y), dyMax = Math_AbsF(origin.Y - maxPos.Y);
		Real32 dzMin = Math_AbsF(origin.Z - minPos.Z), dzMax = Math_AbsF(origin.Z - maxPos.Z);
		Real32 dx = min(dxMin, dxMax), dy = min(dyMin, dyMax), dz = min(dzMin, dzMax);
		if (dx * dx + dy * dy + dz * dz > reachSq) return false;

		tracer.Min = minPos; tracer.Max = maxPos;
		if (intersect(pos)) return true;
		RayTracer_Step(&tracer);
	}

	ErrorHandler_Fail("Something went wrong, did over 10,000 iterations in Picking_RayTrace()");
	return false;
}

static bool Picking_ClipBlock(struct PickedPos* pos) {
	if (!Game_CanPick(tracer.Block)) return false;

	/* This cell falls on the path of the ray. Now perform an additional AABB test,
	since some blocks do not occupy a whole cell. */
	Real32 t0, t1;
	if (!Intersection_RayIntersectsBox(tracer.Origin, tracer.Dir, tracer.Min, tracer.Max, &t0, &t1)) {
		return false;
	}

	Vector3 scaledDir, intersect;
	Vector3_Mul1(&scaledDir, &tracer.Dir, t0);      /* scaledDir = dir * t0 */
	Vector3_Add(&intersect, &tracer.Origin, &scaledDir); /* intersect = origin + scaledDir */
														 /* Only pick the block if the block is precisely within reach distance. */
	Real32 lenSq = Vector3_LengthSquared(&scaledDir);
	Real32 reach = LocalPlayer_Instance.ReachDistance;

	if (lenSq <= reach * reach) {
		PickedPos_SetAsValid(pos, &tracer, intersect);
	} else {
		PickedPos_SetAsInvalid(pos);
	}
	return true;
}

static Vector3 picking_adjust = VECTOR3_CONST1(0.1f);
static bool Picking_ClipCamera(struct PickedPos* pos) {
	if (Block_Draw[tracer.Block] == DRAW_GAS || Block_Collide[tracer.Block] != COLLIDE_SOLID) return false;
	Real32 t0, t1;
	if (!Intersection_RayIntersectsBox(tracer.Origin, tracer.Dir, tracer.Min, tracer.Max, &t0, &t1)) return false;

	/* Need to collide with slightly outside block, to avoid camera clipping issues */
	Vector3_Sub(&tracer.Min, &tracer.Min, &picking_adjust);
	Vector3_Add(&tracer.Max, &tracer.Max, &picking_adjust);
	Intersection_RayIntersectsBox(tracer.Origin, tracer.Dir, tracer.Min, tracer.Max, &t0, &t1);

	Vector3 intersect;
	Vector3_Mul1(&intersect, &tracer.Dir, t0);           /* intersect = dir * t0 */
	Vector3_Add(&intersect, &tracer.Origin, &intersect); /* intersect = origin + dir * t0 */
	PickedPos_SetAsValid(pos, &tracer, intersect);
	return true;
}

void Picking_CalculatePickedBlock(Vector3 origin, Vector3 dir, Real32 reach, struct PickedPos* pos) {
	if (!Picking_RayTrace(origin, dir, reach, pos, Picking_ClipBlock)) {
		PickedPos_SetAsInvalid(pos);
	}
}

void Picking_ClipCameraPos(Vector3 origin, Vector3 dir, Real32 reach, struct PickedPos* pos) {
	bool noClip = !Game_CameraClipping || LocalPlayer_Instance.Hacks.Noclip;
	if (noClip || !Picking_RayTrace(origin, dir, reach, pos, Picking_ClipCamera)) {
		PickedPos_SetAsInvalid(pos);
		Vector3_Mul1(&pos->Intersect, &dir, reach);             /* intersect = dir * reach */
		Vector3_Add(&pos->Intersect, &origin, &pos->Intersect); /* intersect = origin + dir * reach */
	}
}
