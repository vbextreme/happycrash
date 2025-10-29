#ifndef __HAPPYCRASH_H__
#define __HAPPYCRASH_H__

#define HAPPYCRASH_STACK_SIZE     (1024UL*1024UL)
#define HAPPYCRASH_MAGIC          0xC6A5
#define HAPPYCRASH_SECTION        "happycrash.db"
#define HAPPYCRASH_SECTION_FLAGS  "readonly"
#define HAPPYCRASH_BACKTRACE_SIZE 16
#define HAPPYCRASH_BUILDARG_SIZE  1024
#define HAPPY_SKIP     0x01
#define HAPPY_SEGFAULT 0x02
#define HAPPY_FULL     0xFF

#include <stdint.h>
#include <stdlib.h>

typedef struct hcTbl{
	size_t count;
	size_t size;
	char** tbl;
}hcTbl_s;

typedef struct mapAddr{
	uintptr_t start;
	size_t    size;
	unsigned  file;
	unsigned  fn;
	size_t    loc;
}mapAddr_s;

typedef struct hcDB{
	hcTbl_s    file;
	hcTbl_s    fn;
	mapAddr_s* addr;
	size_t     countAddr;
	size_t     sizeAddr;
	char*      buildarg;
	unsigned   version[3];
}hcDB_s;

void happycrash_backtrace(unsigned skip, long inc);
void happycrash_begin(unsigned flags);
void happycrash_panic(const char* format, ...);


























#endif
