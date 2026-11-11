/*
 * Copyright 2018 Avi Halachmi (:avih) avihpit@yahoo.com https://github.com/avih
 * MIT/X Consortium License
 */

/*
 * U+25XX codepoints data
 *
 * References:
 *   http://www.unicode.org/charts/PDF/U2500.pdf
 *   http://www.unicode.org/charts/PDF/U2580.pdf
 *
 * Test page:
 *   https://github.com/GNOME/vte/blob/master/doc/boxes.txt
 */

/* Each shape is encoded as 16-bits. Higher bits are category, lower are data */
/* Categories (mutually exclusive except BDB): */
/* For convenience, BDL/BDA/BBS/BDB are 1 bit each, the rest are enums */
#define BDL (1<<8)   /* Box Draw Lines (light/double/heavy) */
#define BDA (1<<9)   /* Box Draw Arc (light) */

#define BBD (1<<10)  /* Box Block Down (lower) X/8 */
#define BBL (2<<10)  /* Box Block Left X/8 */
#define BBU (3<<10)  /* Box Block Upper X/8 */
#define BBR (4<<10)  /* Box Block Right X/8 */
#define BBQ (5<<10)  /* Box Block Quadrants */
#define BRL (6<<10)  /* Box Braille (data is lower byte of U28XX) */
#define BSX (7<<10)  /* Box Sextant (6-bit mask) */
#define BVS (8<<10)  /* Box Vertical eighth-column Stripe (8-bit mask) */
#define BHS (9<<10)  /* Box Horizontal eighth-row Stripe (8-bit mask) */
#define BES (10<<10) /* Box Edge Stripes (SL/SR/ST/SB mask) */

#define BBS (1<<14)  /* Box Block Shades */
#define BDB (1<<15)  /* Box Draw is Bold */

/* (BDL/BDA) Light/Double/Heavy x Left/Up/Right/Down/Horizontal/Vertical      */
/* Heavy is light+double (literally drawing light+double align to form heavy) */
#define LL (1<<0)
#define LU (1<<1)
#define LR (1<<2)
#define LD (1<<3)
#define LH (LL+LR)
#define LV (LU+LD)

#define DL (1<<4)
#define DU (1<<5)
#define DR (1<<6)
#define DD (1<<7)
#define DH (DL+DR)
#define DV (DU+DD)

#define HL (LL+DL)
#define HU (LU+DU)
#define HR (LR+DR)
#define HD (LD+DD)
#define HH (HL+HR)
#define HV (HU+HD)

/* (BBQ) Quadrants Top/Bottom x Left/Right */
#define TL (1<<0)
#define TR (1<<1)
#define BL (1<<2)
#define BR (1<<3)

/* (BES) Eighth Edge Stripes Top/Bottom x Left/Right */
#define SL (1<<0)
#define SR (1<<1)
#define ST (1<<2)
#define SB (1<<3)

/* Data for U+2500 - U+259F except dashes/diagonals */
static const unsigned short boxdata[256] = {
	/* light lines */
	[0x00] = BDL + LH,       /* light horizontal */
	[0x02] = BDL + LV,       /* light vertical */
	[0x0c] = BDL + LD + LR,  /* light down and right */
	[0x10] = BDL + LD + LL,  /* light down and left */
	[0x14] = BDL + LU + LR,  /* light up and right */
	[0x18] = BDL + LU + LL,  /* light up and left */
	[0x1c] = BDL + LV + LR,  /* light vertical and right */
	[0x24] = BDL + LV + LL,  /* light vertical and left */
	[0x2c] = BDL + LH + LD,  /* light horizontal and down */
	[0x34] = BDL + LH + LU,  /* light horizontal and up */
	[0x3c] = BDL + LV + LH,  /* light vertical and horizontal */
	[0x74] = BDL + LL,       /* light left */
	[0x75] = BDL + LU,       /* light up */
	[0x76] = BDL + LR,       /* light right */
	[0x77] = BDL + LD,       /* light down */

	/* heavy [+light] lines */
	[0x01] = BDL + HH,
	[0x03] = BDL + HV,
	[0x0d] = BDL + HR + LD,
	[0x0e] = BDL + HD + LR,
	[0x0f] = BDL + HD + HR,
	[0x11] = BDL + HL + LD,
	[0x12] = BDL + HD + LL,
	[0x13] = BDL + HD + HL,
	[0x15] = BDL + HR + LU,
	[0x16] = BDL + HU + LR,
	[0x17] = BDL + HU + HR,
	[0x19] = BDL + HL + LU,
	[0x1a] = BDL + HU + LL,
	[0x1b] = BDL + HU + HL,
	[0x1d] = BDL + HR + LV,
	[0x1e] = BDL + HU + LD + LR,
	[0x1f] = BDL + HD + LR + LU,
	[0x20] = BDL + HV + LR,
	[0x21] = BDL + HU + HR + LD,
	[0x22] = BDL + HD + HR + LU,
	[0x23] = BDL + HV + HR,
	[0x25] = BDL + HL + LV,
	[0x26] = BDL + HU + LD + LL,
	[0x27] = BDL + HD + LU + LL,
	[0x28] = BDL + HV + LL,
	[0x29] = BDL + HU + HL + LD,
	[0x2a] = BDL + HD + HL + LU,
	[0x2b] = BDL + HV + HL,
	[0x2d] = BDL + HL + LD + LR,
	[0x2e] = BDL + HR + LL + LD,
	[0x2f] = BDL + HH + LD,
	[0x30] = BDL + HD + LH,
	[0x31] = BDL + HD + HL + LR,
	[0x32] = BDL + HR + HD + LL,
	[0x33] = BDL + HH + HD,
	[0x35] = BDL + HL + LU + LR,
	[0x36] = BDL + HR + LU + LL,
	[0x37] = BDL + HH + LU,
	[0x38] = BDL + HU + LH,
	[0x39] = BDL + HU + HL + LR,
	[0x3a] = BDL + HU + HR + LL,
	[0x3b] = BDL + HH + HU,
	[0x3d] = BDL + HL + LV + LR,
	[0x3e] = BDL + HR + LV + LL,
	[0x3f] = BDL + HH + LV,
	[0x40] = BDL + HU + LH + LD,
	[0x41] = BDL + HD + LH + LU,
	[0x42] = BDL + HV + LH,
	[0x43] = BDL + HU + HL + LD + LR,
	[0x44] = BDL + HU + HR + LD + LL,
	[0x45] = BDL + HD + HL + LU + LR,
	[0x46] = BDL + HD + HR + LU + LL,
	[0x47] = BDL + HH + HU + LD,
	[0x48] = BDL + HH + HD + LU,
	[0x49] = BDL + HV + HL + LR,
	[0x4a] = BDL + HV + HR + LL,
	[0x4b] = BDL + HV + HH,
	[0x78] = BDL + HL,
	[0x79] = BDL + HU,
	[0x7a] = BDL + HR,
	[0x7b] = BDL + HD,
	[0x7c] = BDL + HR + LL,
	[0x7d] = BDL + HD + LU,
	[0x7e] = BDL + HL + LR,
	[0x7f] = BDL + HU + LD,

	/* double [+light] lines */
	[0x50] = BDL + DH,
	[0x51] = BDL + DV,
	[0x52] = BDL + DR + LD,
	[0x53] = BDL + DD + LR,
	[0x54] = BDL + DR + DD,
	[0x55] = BDL + DL + LD,
	[0x56] = BDL + DD + LL,
	[0x57] = BDL + DL + DD,
	[0x58] = BDL + DR + LU,
	[0x59] = BDL + DU + LR,
	[0x5a] = BDL + DU + DR,
	[0x5b] = BDL + DL + LU,
	[0x5c] = BDL + DU + LL,
	[0x5d] = BDL + DL + DU,
	[0x5e] = BDL + DR + LV,
	[0x5f] = BDL + DV + LR,
	[0x60] = BDL + DV + DR,
	[0x61] = BDL + DL + LV,
	[0x62] = BDL + DV + LL,
	[0x63] = BDL + DV + DL,
	[0x64] = BDL + DH + LD,
	[0x65] = BDL + DD + LH,
	[0x66] = BDL + DD + DH,
	[0x67] = BDL + DH + LU,
	[0x68] = BDL + DU + LH,
	[0x69] = BDL + DH + DU,
	[0x6a] = BDL + DH + LV,
	[0x6b] = BDL + DV + LH,
	[0x6c] = BDL + DH + DV,

	/* (light) arcs */
	[0x6d] = BDA + LD + LR,
	[0x6e] = BDA + LD + LL,
	[0x6f] = BDA + LU + LL,
	[0x70] = BDA + LU + LR,

	/* Lower (Down) X/8 block (data is 8 - X) */
	[0x81] = BBD + 7, [0x82] = BBD + 6, [0x83] = BBD + 5, [0x84] = BBD + 4,
	[0x85] = BBD + 3, [0x86] = BBD + 2, [0x87] = BBD + 1, [0x88] = BBD + 0,

	/* Left X/8 block (data is X) */
	[0x89] = BBL + 7, [0x8a] = BBL + 6, [0x8b] = BBL + 5, [0x8c] = BBL + 4,
	[0x8d] = BBL + 3, [0x8e] = BBL + 2, [0x8f] = BBL + 1,

	/* upper 1/2 (4/8), 1/8 block (X), right 1/2, 1/8 block (8-X) */
	[0x80] = BBU + 4, [0x94] = BBU + 1,
	[0x90] = BBR + 4, [0x95] = BBR + 7,

	/* Quadrants */
	[0x96] = BBQ + BL,
	[0x97] = BBQ + BR,
	[0x98] = BBQ + TL,
	[0x99] = BBQ + TL + BL + BR,
	[0x9a] = BBQ + TL + BR,
	[0x9b] = BBQ + TL + TR + BL,
	[0x9c] = BBQ + TL + TR + BR,
	[0x9d] = BBQ + TR,
	[0x9e] = BBQ + BL + TR,
	[0x9f] = BBQ + BL + TR + BR,

	/* Shades, data is an alpha value in 25% units (1/4, 1/2, 3/4) */
	[0x91] = BBS + 1, [0x92] = BBS + 2, [0x93] = BBS + 3,

	/* U+2504 - U+250B, U+254C - U+254F: unsupported (dashes) */
	/* U+2571 - U+2573: unsupported (diagonals) */
};

/* Data for U+1FB00 - U+1FB8B (Legacy Computing: sextants, eighths) */
/* Wedges, shades, diagonals and pictographic symbols (U+1FB3C+, U+1FB8C+)
 * are unsupported - left to font fallback. */
static const unsigned short boxdata_1fb[0x8c] = {
	/* sextants: data is a 6-bit mask, bit0..5 = positions 1..6            */
	/* (1=UL 2=UR 3=ML 4=MR 5=LL 6=LR, per the Unicode SEXTANT-N name)     */
	[0x00] = BSX + 0x01, [0x01] = BSX + 0x02, [0x02] = BSX + 0x03,
	[0x03] = BSX + 0x04, [0x04] = BSX + 0x05, [0x05] = BSX + 0x06,
	[0x06] = BSX + 0x07, [0x07] = BSX + 0x08, [0x08] = BSX + 0x09,
	[0x09] = BSX + 0x0a, [0x0a] = BSX + 0x0b, [0x0b] = BSX + 0x0c,
	[0x0c] = BSX + 0x0d, [0x0d] = BSX + 0x0e, [0x0e] = BSX + 0x0f,
	[0x0f] = BSX + 0x10, [0x10] = BSX + 0x11, [0x11] = BSX + 0x12,
	[0x12] = BSX + 0x13, [0x13] = BSX + 0x14, [0x14] = BSX + 0x16,
	[0x15] = BSX + 0x17, [0x16] = BSX + 0x18, [0x17] = BSX + 0x19,
	[0x18] = BSX + 0x1a, [0x19] = BSX + 0x1b, [0x1a] = BSX + 0x1c,
	[0x1b] = BSX + 0x1d, [0x1c] = BSX + 0x1e, [0x1d] = BSX + 0x1f,
	[0x1e] = BSX + 0x20, [0x1f] = BSX + 0x21, [0x20] = BSX + 0x22,
	[0x21] = BSX + 0x23, [0x22] = BSX + 0x24, [0x23] = BSX + 0x25,
	[0x24] = BSX + 0x26, [0x25] = BSX + 0x27, [0x26] = BSX + 0x28,
	[0x27] = BSX + 0x29, [0x28] = BSX + 0x2b, [0x29] = BSX + 0x2c,
	[0x2a] = BSX + 0x2d, [0x2b] = BSX + 0x2e, [0x2c] = BSX + 0x2f,
	[0x2d] = BSX + 0x30, [0x2e] = BSX + 0x31, [0x2f] = BSX + 0x32,
	[0x30] = BSX + 0x33, [0x31] = BSX + 0x34, [0x32] = BSX + 0x35,
	[0x33] = BSX + 0x36, [0x34] = BSX + 0x37, [0x35] = BSX + 0x38,
	[0x36] = BSX + 0x39, [0x37] = BSX + 0x3a, [0x38] = BSX + 0x3b,
	[0x39] = BSX + 0x3c, [0x3a] = BSX + 0x3d, [0x3b] = BSX + 0x3e,

	/* single eighth-column / eighth-row stripes (not edge-cumulative) */
	[0x70] = BVS + 0x02, /* vertical eighth stripe, column 2 */
	[0x71] = BVS + 0x04, /* vertical eighth stripe, column 3 */
	[0x72] = BVS + 0x08, /* vertical eighth stripe, column 4 */
	[0x73] = BVS + 0x10, /* vertical eighth stripe, column 5 */
	[0x74] = BVS + 0x20, /* vertical eighth stripe, column 6 */
	[0x75] = BVS + 0x40, /* vertical eighth stripe, column 7 */
	[0x76] = BHS + 0x02, /* horizontal eighth stripe, row 2 */
	[0x77] = BHS + 0x04, /* horizontal eighth stripe, row 3 */
	[0x78] = BHS + 0x08, /* horizontal eighth stripe, row 4 */
	[0x79] = BHS + 0x10, /* horizontal eighth stripe, row 5 */
	[0x7a] = BHS + 0x20, /* horizontal eighth stripe, row 6 */
	[0x7b] = BHS + 0x40, /* horizontal eighth stripe, row 7 */

	/* mixed-axis edge stripe pairs */
	[0x7c] = BES + SL + SB,
	[0x7d] = BES + SL + ST,
	[0x7e] = BES + SR + ST,
	[0x7f] = BES + SR + SB,
	[0x80] = BES + ST + SB,

	/* horizontal eighth stripe, rows 1/3/5/8 (placed here for codepoint order) */
	[0x81] = BHS + 0x95,

	/* cumulative eighths, filling gaps left by U+2500's halves/full-eighths */
	[0x82] = BBU + 2,
	[0x83] = BBU + 3,
	[0x84] = BBU + 5,
	[0x85] = BBU + 6,
	[0x86] = BBU + 7,
	[0x87] = BBR + 6,
	[0x88] = BBR + 5,
	[0x89] = BBR + 3,
	[0x8a] = BBR + 2,
	[0x8b] = BBR + 1,
};
