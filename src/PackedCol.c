#include "PackedCol.h"
#include "ExtMath.h"

PackedCol PackedCol_Create4(UInt8 r, UInt8 g, UInt8 b, UInt8 a) {
	PackedCol col;
	col.R = r; col.G = g; col.B = b; col.A = a;
	return col;
}

PackedCol PackedCol_Create3(UInt8 r, UInt8 g, UInt8 b) {
	PackedCol col;
	col.R = r; col.G = g; col.B = b; col.A = 255;
	return col;
}

UInt32 PackedCol_ToARGB(PackedCol col) {
	return PackedCol_ARGB(col.R, col.G, col.B, col.A);
}

PackedCol PackedCol_Scale(PackedCol value, Real32 t) {
	value.R = (UInt8)(value.R * t);
	value.G = (UInt8)(value.G * t);
	value.B = (UInt8)(value.B * t);
	return value;
}

PackedCol PackedCol_Lerp(PackedCol a, PackedCol b, Real32 t) {
	a.R = (UInt8)Math_Lerp(a.R, b.R, t);
	a.G = (UInt8)Math_Lerp(a.G, b.G, t);
	a.B = (UInt8)Math_Lerp(a.B, b.B, t);
	return a;
}

void PackedCol_GetShaded(PackedCol normal, PackedCol* xSide, PackedCol* zSide, PackedCol* yMin) {
	*xSide = PackedCol_Scale(normal, PACKEDCOL_SHADE_X);
	*zSide = PackedCol_Scale(normal, PACKEDCOL_SHADE_Z);
	*yMin  = PackedCol_Scale(normal, PACKEDCOL_SHADE_YMIN);
}

bool PackedCol_Unhex(UInt8 hex, Int32* value) {
	*value = 0;
	if (hex >= '0' && hex <= '9') {
		*value = (hex - '0');
	} else if (hex >= 'a' && hex <= 'f') {
		*value = (hex - 'a') + 10;
	} else if (hex >= 'A' && hex <= 'F') {
		*value = (hex - 'A') + 10;
	} else {
		return false;
	}
	return true;
}

void PackedCol_ToHex(STRING_TRANSIENT String* str, PackedCol value) {
	UInt8 input[3] = { value.R, value.G, value.B };
	UChar hex[7];
	Int32 i;

	for (i = 0; i < 3; i++) {
		Int32 cur = input[i], hi = cur >> 4, lo = cur & 0x0F;
		/* 48 = index of 0, 55 = index of (A - 10) */
		hex[i * 2 + 0] = hi < 10 ? (hi + 48) : (hi + 55);
		hex[i * 2 + 1] = lo < 10 ? (lo + 48) : (lo + 55);
	}

	hex[6] = '\0';
	String_AppendConst(str, hex);
}

bool PackedCol_TryParseHex(STRING_PURE String* str, PackedCol* value) {
	PackedCol empty = PACKEDCOL_CONST(0, 0, 0, 0); *value = empty;
	/* accept XXYYZZ or #XXYYZZ forms */
	if (str->length < 6) return false;
	if (str->length > 6 && (str->buffer[0] != '#' || str->length > 7)) return false;

	Int32 rH, rL, gH, gL, bH, bL;
	UChar* buffer = str->buffer;
	if (buffer[0] == '#') buffer++;

	if (!PackedCol_Unhex(buffer[0], &rH) || !PackedCol_Unhex(buffer[1], &rL)) return false;
	if (!PackedCol_Unhex(buffer[2], &gH) || !PackedCol_Unhex(buffer[3], &gL)) return false;
	if (!PackedCol_Unhex(buffer[4], &bH) || !PackedCol_Unhex(buffer[5], &bL)) return false;

	*value = PackedCol_Create3((rH << 4) | rL, (gH << 4) | gL, (bH << 4) | bL);
	return true;
}
