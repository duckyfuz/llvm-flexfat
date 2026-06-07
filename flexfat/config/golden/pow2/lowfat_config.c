/* AUTOMATICALLY GENERATED */

#include "lowfat_config.h"

#define LOWFAT_IS_POW2 1
#define LOWFAT_NUM_REGIONS 30
#define LOWFAT_SIZES_PAGES 1
#define LOWFAT_REGION_SIZE _LOWFAT_REGION_SIZE
#define LOWFAT_REGION_SIZE_SHIFT 35
#define LOWFAT_STACK_MEMORY_SIZE 8589934592
#define LOWFAT_GLOBAL_MEMORY_SIZE 8589934592
#define LOWFAT_HEAP_MEMORY_SIZE 17179803648
#define LOWFAT_STACK_MEMORY_OFFSET 25769803776
#define LOWFAT_GLOBAL_MEMORY_OFFSET 17179832320
#define LOWFAT_HEAP_MEMORY_OFFSET 0
#define LOWFAT_STACK_SIZE 67108864
#define LOWFAT_PAGE_SIZE 4096
#define LOWFAT_HEAP_ASLR_MASK 0xFFFFFFFFull
#define LOWFAT_MAX_HEAP_ALLOC_SIZE 8589934592
#define LOWFAT_TID_OFFSET 0x2d0
#define LOWFAT_JOINID_OFFSET 0x620
#define LOWFAT_MAX_STACK_ALLOC_SIZE 33554432
#define LOWFAT_MAX_GLOBAL_ALLOC_SIZE 67108864
#define LOWFAT_MIN_ALLOC_SIZE 16
#define LOWFAT_NUM_STACK_REGIONS 22
#define LOWFAT_STACK_REGION 31
#define LOWFAT_CONST_DATA __attribute__((__section__("LOWFAT_CONST_DATA")))

static const LOWFAT_CONST_DATA size_t lowfat_sizes[] =
{
	16, /* idx=0 */
	32, /* idx=1 */
	64, /* idx=2 */
	128, /* idx=3 */
	256, /* idx=4 */
	512, /* idx=5 */
	1024, /* idx=6 */
	2048, /* idx=7 */
	4096, /* idx=8 */
	8192, /* idx=9 */
	16384, /* idx=10 */
	32768, /* idx=11 */
	65536, /* idx=12 */
	131072, /* idx=13 */
	262144, /* idx=14 */
	524288, /* idx=15 */
	1048576, /* idx=16 */
	2097152, /* idx=17 */
	4194304, /* idx=18 */
	8388608, /* idx=19 */
	16777216, /* idx=20 */
	33554432, /* idx=21 */
	67108864, /* idx=22 */
	134217728, /* idx=23 */
	268435456, /* idx=24 */
	536870912, /* idx=25 */
	1073741824, /* idx=26 */
	2147483648, /* idx=27 */
	4294967296, /* idx=28 */
	8589934592, /* idx=29 */
};

static const LOWFAT_CONST_DATA size_t lowfat_magics[] =
{
	0xFFFFFFFFFFFFFFF0ull, /* idx=0, size=16 */
	0xFFFFFFFFFFFFFFE0ull, /* idx=1, size=32 */
	0xFFFFFFFFFFFFFFC0ull, /* idx=2, size=64 */
	0xFFFFFFFFFFFFFF80ull, /* idx=3, size=128 */
	0xFFFFFFFFFFFFFF00ull, /* idx=4, size=256 */
	0xFFFFFFFFFFFFFE00ull, /* idx=5, size=512 */
	0xFFFFFFFFFFFFFC00ull, /* idx=6, size=1024 */
	0xFFFFFFFFFFFFF800ull, /* idx=7, size=2048 */
	0xFFFFFFFFFFFFF000ull, /* idx=8, size=4096 */
	0xFFFFFFFFFFFFE000ull, /* idx=9, size=8192 */
	0xFFFFFFFFFFFFC000ull, /* idx=10, size=16384 */
	0xFFFFFFFFFFFF8000ull, /* idx=11, size=32768 */
	0xFFFFFFFFFFFF0000ull, /* idx=12, size=65536 */
	0xFFFFFFFFFFFE0000ull, /* idx=13, size=131072 */
	0xFFFFFFFFFFFC0000ull, /* idx=14, size=262144 */
	0xFFFFFFFFFFF80000ull, /* idx=15, size=524288 */
	0xFFFFFFFFFFF00000ull, /* idx=16, size=1048576 */
	0xFFFFFFFFFFE00000ull, /* idx=17, size=2097152 */
	0xFFFFFFFFFFC00000ull, /* idx=18, size=4194304 */
	0xFFFFFFFFFF800000ull, /* idx=19, size=8388608 */
	0xFFFFFFFFFF000000ull, /* idx=20, size=16777216 */
	0xFFFFFFFFFE000000ull, /* idx=21, size=33554432 */
	0xFFFFFFFFFC000000ull, /* idx=22, size=67108864 */
	0xFFFFFFFFF8000000ull, /* idx=23, size=134217728 */
	0xFFFFFFFFF0000000ull, /* idx=24, size=268435456 */
	0xFFFFFFFFE0000000ull, /* idx=25, size=536870912 */
	0xFFFFFFFFC0000000ull, /* idx=26, size=1073741824 */
	0xFFFFFFFF80000000ull, /* idx=27, size=2147483648 */
	0xFFFFFFFF00000000ull, /* idx=28, size=4294967296 */
	0xFFFFFFFE00000000ull, /* idx=29, size=8589934592 */
};

static const LOWFAT_CONST_DATA size_t lowfat_stacks[] =
{
	1,
	2,
	3,
	4,
	5,
	6,
	7,
	8,
	9,
	10,
	11,
	12,
	13,
	14,
	15,
	16,
	17,
	18,
	19,
	20,
	21,
	22,
	31,
	0,
};

const LOWFAT_CONST_DATA size_t lowfat_stack_sizes[64+1] =
{
	0, /* idx=0 */
	0, /* idx=1 */
	0, /* idx=2 */
	0, /* idx=3 */
	0, /* idx=4 */
	0, /* idx=5 */
	0, /* idx=6 */
	0, /* idx=7 */
	0, /* idx=8 */
	0, /* idx=9 */
	0, /* idx=10 */
	0, /* idx=11 */
	0, /* idx=12 */
	0, /* idx=13 */
	0, /* idx=14 */
	0, /* idx=15 */
	0, /* idx=16 */
	0, /* idx=17 */
	0, /* idx=18 */
	0, /* idx=19 */
	0, /* idx=20 */
	0, /* idx=21 */
	0, /* idx=22 */
	0, /* idx=23 */
	0, /* idx=24 */
	0, /* idx=25 */
	0, /* idx=26 */
	0, /* idx=27 */
	0, /* idx=28 */
	0, /* idx=29 */
	0, /* idx=30 */
	0, /* idx=31 */
	0, /* idx=32 */
	0, /* idx=33 */
	0, /* idx=34 */
	0, /* idx=35 */
	0, /* idx=36 */
	0, /* idx=37 */
	0, /* idx=38 */
	33554432, /* idx=39 */
	16777216, /* idx=40 */
	8388608, /* idx=41 */
	4194304, /* idx=42 */
	2097152, /* idx=43 */
	1048576, /* idx=44 */
	524288, /* idx=45 */
	262144, /* idx=46 */
	131072, /* idx=47 */
	65536, /* idx=48 */
	32768, /* idx=49 */
	16384, /* idx=50 */
	8192, /* idx=51 */
	4096, /* idx=52 */
	2048, /* idx=53 */
	1024, /* idx=54 */
	512, /* idx=55 */
	256, /* idx=56 */
	128, /* idx=57 */
	64, /* idx=58 */
	32, /* idx=59 */
	16, /* idx=60 */
	16, /* idx=61 */
	16, /* idx=62 */
	16, /* idx=63 */
	16, /* idx=64 */
};

const LOWFAT_CONST_DATA size_t lowfat_stack_masks[64+1] =
{
	0, /* idx=0 */
	0, /* idx=1 */
	0, /* idx=2 */
	0, /* idx=3 */
	0, /* idx=4 */
	0, /* idx=5 */
	0, /* idx=6 */
	0, /* idx=7 */
	0, /* idx=8 */
	0, /* idx=9 */
	0, /* idx=10 */
	0, /* idx=11 */
	0, /* idx=12 */
	0, /* idx=13 */
	0, /* idx=14 */
	0, /* idx=15 */
	0, /* idx=16 */
	0, /* idx=17 */
	0, /* idx=18 */
	0, /* idx=19 */
	0, /* idx=20 */
	0, /* idx=21 */
	0, /* idx=22 */
	0, /* idx=23 */
	0, /* idx=24 */
	0, /* idx=25 */
	0, /* idx=26 */
	0, /* idx=27 */
	0, /* idx=28 */
	0, /* idx=29 */
	0, /* idx=30 */
	0, /* idx=31 */
	0, /* idx=32 */
	0, /* idx=33 */
	0, /* idx=34 */
	0, /* idx=35 */
	0, /* idx=36 */
	0, /* idx=37 */
	0, /* idx=38 */
	0xFFFFFFFFFE000000ull,	/* idx=39, size=33554432 */
	0xFFFFFFFFFF000000ull,	/* idx=40, size=16777216 */
	0xFFFFFFFFFF800000ull,	/* idx=41, size=8388608 */
	0xFFFFFFFFFFC00000ull,	/* idx=42, size=4194304 */
	0xFFFFFFFFFFE00000ull,	/* idx=43, size=2097152 */
	0xFFFFFFFFFFF00000ull,	/* idx=44, size=1048576 */
	0xFFFFFFFFFFF80000ull,	/* idx=45, size=524288 */
	0xFFFFFFFFFFFC0000ull,	/* idx=46, size=262144 */
	0xFFFFFFFFFFFE0000ull,	/* idx=47, size=131072 */
	0xFFFFFFFFFFFF0000ull,	/* idx=48, size=65536 */
	0xFFFFFFFFFFFF8000ull,	/* idx=49, size=32768 */
	0xFFFFFFFFFFFFC000ull,	/* idx=50, size=16384 */
	0xFFFFFFFFFFFFE000ull,	/* idx=51, size=8192 */
	0xFFFFFFFFFFFFF000ull,	/* idx=52, size=4096 */
	0xFFFFFFFFFFFFF800ull,	/* idx=53, size=2048 */
	0xFFFFFFFFFFFFFC00ull,	/* idx=54, size=1024 */
	0xFFFFFFFFFFFFFE00ull,	/* idx=55, size=512 */
	0xFFFFFFFFFFFFFF00ull,	/* idx=56, size=256 */
	0xFFFFFFFFFFFFFF80ull,	/* idx=57, size=128 */
	0xFFFFFFFFFFFFFFC0ull,	/* idx=58, size=64 */
	0xFFFFFFFFFFFFFFE0ull,	/* idx=59, size=32 */
	0xFFFFFFFFFFFFFFF0ull,	/* idx=60, size=16 */
	0xFFFFFFFFFFFFFFF0ull,	/* idx=61, size=16 */
	0xFFFFFFFFFFFFFFF0ull,	/* idx=62, size=16 */
	0xFFFFFFFFFFFFFFF0ull,	/* idx=63, size=16 */
	0xFFFFFFFFFFFFFFF0ull,	/* idx=64, size=16 */
};

const LOWFAT_CONST_DATA ssize_t lowfat_stack_offsets[64+1] =
{
	0, /* idx=0 */
	0, /* idx=1 */
	0, /* idx=2 */
	0, /* idx=3 */
	0, /* idx=4 */
	0, /* idx=5 */
	0, /* idx=6 */
	0, /* idx=7 */
	0, /* idx=8 */
	0, /* idx=9 */
	0, /* idx=10 */
	0, /* idx=11 */
	0, /* idx=12 */
	0, /* idx=13 */
	0, /* idx=14 */
	0, /* idx=15 */
	0, /* idx=16 */
	0, /* idx=17 */
	0, /* idx=18 */
	0, /* idx=19 */
	0, /* idx=20 */
	0, /* idx=21 */
	0, /* idx=22 */
	0, /* idx=23 */
	0, /* idx=24 */
	0, /* idx=25 */
	0, /* idx=26 */
	0, /* idx=27 */
	0, /* idx=28 */
	0, /* idx=29 */
	0, /* idx=30 */
	0, /* idx=31 */
	0, /* idx=32 */
	0, /* idx=33 */
	0, /* idx=34 */
	0, /* idx=35 */
	0, /* idx=36 */
	0, /* idx=37 */
	0, /* idx=38 */
	-309237645312,	/* idx=39, size=33554432 */
	-343597383680,	/* idx=40, size=16777216 */
	-377957122048,	/* idx=41, size=8388608 */
	-412316860416,	/* idx=42, size=4194304 */
	-446676598784,	/* idx=43, size=2097152 */
	-481036337152,	/* idx=44, size=1048576 */
	-515396075520,	/* idx=45, size=524288 */
	-549755813888,	/* idx=46, size=262144 */
	-584115552256,	/* idx=47, size=131072 */
	-618475290624,	/* idx=48, size=65536 */
	-652835028992,	/* idx=49, size=32768 */
	-687194767360,	/* idx=50, size=16384 */
	-721554505728,	/* idx=51, size=8192 */
	-755914244096,	/* idx=52, size=4096 */
	-790273982464,	/* idx=53, size=2048 */
	-824633720832,	/* idx=54, size=1024 */
	-858993459200,	/* idx=55, size=512 */
	-893353197568,	/* idx=56, size=256 */
	-927712935936,	/* idx=57, size=128 */
	-962072674304,	/* idx=58, size=64 */
	-996432412672,	/* idx=59, size=32 */
	-1030792151040,	/* idx=60, size=16 */
	-1030792151040,	/* idx=61, size=16 */
	-1030792151040,	/* idx=62, size=16 */
	-1030792151040,	/* idx=63, size=16 */
	-1030792151040,	/* idx=64, size=16 */
};

static size_t lowfat_heap_select(size_t size)
{
	switch (__builtin_clzll(size))
	{
		case 64:
		case 63:
		case 62:
		case 61:
		case 60:
		case 59:
			if (size <= 16-1-0)
				return 1;
		case 58:
			if (size <= 32-1-0)
				return 2;
		case 57:
			if (size <= 64-1-0)
				return 3;
		case 56:
			if (size <= 128-1-0)
				return 4;
		case 55:
			if (size <= 256-1-0)
				return 5;
		case 54:
			if (size <= 512-1-0)
				return 6;
		case 53:
			if (size <= 1024-1-0)
				return 7;
		case 52:
			if (size <= 2048-1-0)
				return 8;
		case 51:
			if (size <= 4096-1-0)
				return 9;
		case 50:
			if (size <= 8192-1-0)
				return 10;
		case 49:
			if (size <= 16384-1-0)
				return 11;
		case 48:
			if (size <= 32768-1-0)
				return 12;
		case 47:
			if (size <= 65536-1-0)
				return 13;
		case 46:
			if (size <= 131072-1-0)
				return 14;
		case 45:
			if (size <= 262144-1-0)
				return 15;
		case 44:
			if (size <= 524288-1-0)
				return 16;
		case 43:
			if (size <= 1048576-1-0)
				return 17;
		case 42:
			if (size <= 2097152-1-0)
				return 18;
		case 41:
			if (size <= 4194304-1-0)
				return 19;
		case 40:
			if (size <= 8388608-1-0)
				return 20;
		case 39:
			if (size <= 16777216-1-0)
				return 21;
		case 38:
			if (size <= 33554432-1-0)
				return 22;
		case 37:
			if (size <= 67108864-1-0)
				return 23;
		case 36:
			if (size <= 134217728-1-0)
				return 24;
		case 35:
			if (size <= 268435456-1-0)
				return 25;
		case 34:
			if (size <= 536870912-1-0)
				return 26;
		case 33:
			if (size <= 1073741824-1-0)
				return 27;
		case 32:
			if (size <= 2147483648-1-0)
				return 28;
		case 31:
			if (size <= 4294967296-1-0)
				return 29;
		case 30:
			if (size <= 8589934592-1-0)
				return 30;
		case 29:
		case 28:
		case 27:
		case 26:
		case 25:
		case 24:
		case 23:
		case 22:
		case 21:
		case 20:
		case 19:
		case 18:
		case 17:
		case 16:
		case 15:
		case 14:
		case 13:
		case 12:
		case 11:
		case 10:
		case 9:
		case 8:
		case 7:
		case 6:
		case 5:
		case 4:
		case 3:
		case 2:
		case 1:
		case 0:
		default:
			return 0;
	}
}

