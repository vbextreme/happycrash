#define _GNU_SOURCE
#include <happycrash.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <libelf.h>
#include <gelf.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <execinfo.h>
#include <limits.h>

#define MAXFILENAME 512

#define FG(COLOR) "\x1b[38;5;" #COLOR "m"
#define BG(COLOR) "\x1b[48;5;" #COLOR "m"
#define RESET()   "\x1b[0m"

typedef struct hcMaps{
	char      fname[MAXFILENAME];
	uintptr_t staddr;
	uintptr_t enaddr;
	uintptr_t offset;
	char*     section;
	size_t    size;
}hcMaps_s;

typedef struct hc{
	hcMaps_s* maps;
	size_t    size;
	size_t    count;
	size_t    msize;
	void*     mtmp;
	hcDB_s*   db;
	unsigned  curmap;
	unsigned  flags;
}hc_s;

static hc_s* HC;
static stack_t SIGSTACK;
static char SSTACK[HAPPYCRASH_STACK_SIZE];
static int ISTTY;

static void* page_alloc(size_t size){
	void* ret = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if( ret == MAP_FAILED ){
		fprintf(stderr, "unable to allocate memory: %m\n");
		exit(1);
	}
	return ret;
}

static void* page_realloc(void* old, size_t oldsize, size_t newsize){
	old = mremap(old, oldsize, newsize, MREMAP_MAYMOVE);
	if( old == MAP_FAILED ){
		fprintf(stderr, "unable remap page: %m\n");
		exit(1);
	}
	return old;
}

static void page_protect(void* addr, size_t size, unsigned protect){
	int ret = 0;
	if( protect ){
		ret = mprotect(addr, size, PROT_NONE);
	}
	else{
		ret = mprotect(addr, size, PROT_READ | PROT_WRITE);
	}
	if( ret ){
		fprintf(stderr, "mprotect fail: %m\n");
		exit(1);
	}
}

static uintptr_t hc_maps_addr(const char** line){
	char* en = NULL;
	errno = 0;
	uintptr_t ret = strtoul(*line, &en, 16);
	if( !en || errno ){
		fprintf(stderr, "invalid maps address<%s>: %m\n", *line);
		exit(1);
	}
	*line = en;
	return ret;
}

static const char* hc_maps_skip(const char* line){
	while( *line != ' ' && *line ) ++line;
	return line+1;
}

static char* find_section(const char* path, size_t* size){
	for( unsigned i = 0; i < HC->count; ++i ){
		if( !strcmp(HC->maps[i].fname, path) ){
			*size = HC->maps[i].size;
			return HC->maps[i].section;
		}
	}
	return NULL;
}

static char* load_section(const char* path, size_t* size){
	void* sec = find_section(path, size);
	if( sec ) return sec;
	
	*size = 0;
	if( elf_version(EV_CURRENT) == EV_NONE ){
		fprintf(stderr, "libelf not initialized\n");
		exit(1);
	}
	int fd = open(path, O_RDONLY);
	if( fd < 0 ) return NULL;
	Elf *elf = elf_begin(fd, ELF_C_READ, NULL);
	if( !elf ){
		close(fd);
		return NULL;
	}
	size_t shstrndx;
	if( elf_getshdrstrndx(elf, &shstrndx) != 0 ){
		goto ONERR;
	}
	Elf_Scn *scn = NULL;
	GElf_Shdr shdr;
	char *name;
	while( (scn = elf_nextscn(elf, scn)) != NULL){
		if( gelf_getshdr(scn, &shdr) != &shdr ) continue;
		name = elf_strptr(elf, shstrndx, shdr.sh_name);
		if( name && !strcmp(name, HAPPYCRASH_SECTION) ){
			Elf_Data *data = elf_getdata(scn, NULL);
			if (!data || !data->d_buf) {
				goto ONERR;
			}
			sec = page_alloc(data->d_size);
			memcpy(sec, data->d_buf, data->d_size);
			*size = data->d_size;
			elf_end(elf);
			close(fd);
			page_protect(sec, *size, 1);
			return sec;
		}
	}
ONERR:
	elf_end(elf);
	close(fd);
	return NULL;
}

static void hc_parse_maps_line(hcMaps_s* hcm, const char* line){
	hcm->staddr = hc_maps_addr(&line);
	if( *line++ != '-' ){
		fprintf(stderr, "happycrash wrong maps file, aspected '-'\n");
		exit(1);
	}
	hcm->enaddr = hc_maps_addr(&line);
	if( *line++ != ' ' ){
		fprintf(stderr, "happycrash wrong maps file, aspected ' '\n");
		exit(1);
	}
	line = hc_maps_skip(line);
	hcm->offset = hc_maps_addr(&line);
	if( *line++ != ' ' ){
		fprintf(stderr,"happycrash wrong maps file, aspected ' '\n");
		exit(1);
	}
	line = hc_maps_skip(line);
	line = hc_maps_skip(line);
	while( *line && (*line == ' ' || *line == '\t') ) ++line;
	const char* endname = strchrnul(line, '\n');
	size_t fnamelen = endname - line;
	if( fnamelen >= MAXFILENAME-1 ){
		fprintf(stderr, "happycrash path to max\n");
		exit(1);
	}
	if( fnamelen > 0 ){
		memcpy(hcm->fname, line, fnamelen);
	}
	hcm->fname[fnamelen] = 0;
	//fprintf(stderr, "%lX-%lX +%lX %s\n", hcm->staddr, hcm->enaddr, hcm->offset, hcm->fname);
	hcm->section = load_section(hcm->fname, &hcm->size);
	if( hcm->section ){
		if( hcm->size > HC->msize ) HC->msize = hcm->size;
	}
}

static void hc_ctor(void){
	HC = page_alloc(sizeof(hc_s));
	HC->count = 0;
	HC->size  = 64;
	HC->maps  = page_alloc(HC->size * sizeof(hcMaps_s));
	HC->msize = 0;
	HC->curmap= UINT_MAX;
}

static void hc_maps_grow(void){
	if( HC->count >= HC->size ){
		if( HC->count >= UINT_MAX-1 ){
			fprintf(stderr, "to many maps");
			exit(1);
		}
		size_t oldsize = HC->size * sizeof(hcMaps_s);
		HC->size *= 2;
		HC->maps  = page_realloc(HC->maps, oldsize, HC->size * sizeof(hcMaps_s));
	}
}

static void hc_maps(void){
	FILE* f = fopen("/proc/self/maps", "r");
	if( !f ){
		fprintf(stderr, "happycrash unable to open '/proc/self/maps': %m\n");
		exit(1);
	}
	char buf[4096];
	while( fgets(buf, 4096, f) ){
		hc_maps_grow();
		hc_parse_maps_line(&HC->maps[HC->count++], buf);
	}
	fclose(f);
	page_protect(HC->maps, HC->size * sizeof(hcMaps_s), 1);
}

static void hc_begin(unsigned flags){
	hc_ctor();
	hc_maps();
	if( HC->msize == 0 ){
		fprintf(stderr, "unable to allocate tmp memory, not have any happycrash db");
		exit(1);
	}
	HC->mtmp = page_alloc(HC->msize);
	HC->flags = flags;
	page_protect(HC, sizeof(hc_s), 1);
}

static void hc_break_condom(void){
	page_protect(HC, sizeof(hc_s), 0);
	page_protect(HC->maps, HC->size * sizeof(hcMaps_s), 0);
	for( unsigned i = 0; i < HC->count; ++i ){
		if( HC->maps[i].section ){
			page_protect(HC->maps[i].section, HC->maps[i].size, 0);
		}
	}
}

static long hc_maps_find(uintptr_t addr){
	for( unsigned i = 0; i < HC->count; ++i ){
		if( addr >= HC->maps[i].staddr && addr <= HC->maps[i].enaddr ){
			return i;
		}
	}
	return -1;
}

static uint16_t r16(char** p){
	uint16_t v;
	memcpy(&v, *p, 2);
	(*p)+=2;
	return v;
}

static uint64_t rnb(char** p){
	uint64_t val = 0;
	char* a = *p;
	while( *a & 0x80 ){
		val |= *a & 0x7F;
		val <<= 7;
		++a;
	};
	val |= *a++;
	*p = a;
	return val;
}

static char* rstr(char** p){
	char* s = *p;
	size_t len = strlen(s);
	*p += len+1;
	return s;
}

static void* loc_alloc(void* mem, size_t* used, size_t maxsize, size_t size){
	if( *used + size + 16 > maxsize ){
		fprintf(stderr, "end of local memory\n");
		exit(1);
	}
	uintptr_t addr = (uintptr_t)mem;
	addr += *used;
	*used += size;
	unsigned unaligned = addr % 16;
	if( unaligned ){
		unaligned = 16-unaligned;
		addr += unaligned;
		*used += unaligned;
	}
	if( addr % 16 ){
		fprintf(stderr, "internal error on aligned address\n");
		exit(1);
	}
	return (void*)addr;
}

static void hc_load_db_map(unsigned im){
	if( HC->curmap == im ) return;
	char* p = HC->maps[im].section;
	size_t used = 0;
	HC->db = loc_alloc(HC->mtmp, &used, HC->msize, sizeof(hcDB_s));
	if( r16(&p) != HAPPYCRASH_MAGIC ){
		fprintf(stderr, "invalid happy crash format\n");
		exit(1);
	}
	HC->db->version[0] = rnb(&p);
	HC->db->version[1] = rnb(&p);
	HC->db->version[2] = rnb(&p);
	HC->db->buildarg   = rstr(&p);
	HC->db->file.count = rnb(&p);
	HC->db->file.tbl   = loc_alloc(HC->mtmp, &used, HC->msize, sizeof(char*) * HC->db->file.count);
	for( unsigned i = 0; i < HC->db->file.count; ++i ){
		HC->db->file.tbl[i] = rstr(&p);
	}
	HC->db->fn.count = rnb(&p);
	HC->db->fn.tbl   = loc_alloc(HC->mtmp, &used, HC->msize, sizeof(char*) * HC->db->fn.count);
	for( unsigned i = 0; i < HC->db->fn.count; ++i ){
		HC->db->fn.tbl[i] = rstr(&p);
	}
	HC->db->sizeAddr  = rnb(&p);
	HC->db->countAddr = rnb(&p);
	HC->db->addr      = (void*)p;
	HC->curmap = im;
}

static void rmapaddr(mapAddr_s* ma, char** p){
	ma->start = rnb(p);
	ma->size  = rnb(p);
	ma->file  = rnb(p);
	ma->fn    = rnb(p);
	ma->loc   = rnb(p);
}

static long hc_find_addr(mapAddr_s* ma, uintptr_t reladdr){
	char* stad = (char*)HC->db->addr;
	uintptr_t addr = HC->db->sizeAddr;
	for( size_t i = 0; i < HC->db->countAddr; ++i ){
		rmapaddr(ma, &stad);
		ma->start += addr;
		addr = ma->start;
		if( reladdr >= ma->start && reladdr < ma->start+ma->size ){
			return 0;
		}
	}
	return -1;
}

void happycrash_backtrace(unsigned skip, long inc){
	dprintf(STDERR_FILENO, "stack trace:\n");
	const char* tri = "??";
	void *array[HAPPYCRASH_BACKTRACE_SIZE];
	const size_t size = backtrace(array, HAPPYCRASH_BACKTRACE_SIZE);
	if( inc >= 0 && inc < (long)size ){
		array[inc] = (void*)((uintptr_t)array[inc] + 1);
	}
	for( unsigned i = skip; i < size; ++i ){
		const uintptr_t addr = (uintptr_t)array[i] - 1;
		const long imap = hc_maps_find(addr);
		unsigned bti = i-skip;
		if( imap == -1 ){
			dprintf(STDERR_FILENO, "[%2u] %s::%s:%s() %s\n", bti, tri, tri, tri, tri);
		}
		else if( HC->maps[imap].section == NULL ){
			dprintf(STDERR_FILENO, "[%2u] %s::%s:%s() %s\n", bti, HC->maps[imap].fname, tri, tri, tri);
		}
		else{
			hc_load_db_map(imap);
			const uintptr_t reladdr = (addr - HC->maps[imap].staddr) + HC->maps[imap].offset;
			mapAddr_s m;
			if( hc_find_addr(&m, reladdr) ){
				dprintf(STDERR_FILENO, "[%2u] %s.%u.%u.%u::%s:%s() %s\n",
					bti,
					HC->maps[imap].fname,
					HC->db->version[0], HC->db->version[1], HC->db->version[2],
					tri, tri, tri
				);
			}
			else{
				dprintf(STDERR_FILENO, "[%2u] %s.%u.%u.%u::%s:%s() %lu\n",
					bti,
					HC->maps[imap].fname,
					HC->db->version[0], HC->db->version[1], HC->db->version[2],
					HC->db->file.tbl[m.file],
					HC->db->fn.tbl[m.fn],
					m.loc
				);
			}
		}
	}
}

static void hc_print_welcome(void){
	if( ISTTY ){
		dprintf(STDERR_FILENO, "Welcome to " FG(51)"Happy" FG(226)" Crash"RESET()":\nSomething went wrong " FG(9)"😢"RESET() ", " BG(23)"please report this to developer"RESET() "\n");
	}
	else{
		dprintf(STDERR_FILENO, "Welcome to Happy Crash:\nSomething went wrong 😢, please report this to developer\n");
	}
}

static void hc_print_buildarg(void){
	for( unsigned i = 0; i < HC->count; ++i ){
		if( HC->maps[i].section ){
			hc_load_db_map(i);
			dprintf(STDERR_FILENO, "buildarg: %s::\n%s\n", HC->maps[i].fname, HC->db->buildarg);
			return;
		}
	}
}

static void hc_print_anybye(void){
	dprintf(STDERR_FILENO, "any bye " FG(51)"😄"RESET()"\n");
}

static void happy_crash_time(int sig, [[maybe_unused]] siginfo_t *info, [[maybe_unused]] void *ucontext){
	hc_break_condom();
	hc_print_welcome();
	hc_print_buildarg();
	if( ISTTY ){
		dprintf(STDERR_FILENO, FG(9)"fatal signal"RESET() ": %s(%u)\n", strsignal(sig), sig);
	}
	else{
		dprintf(STDERR_FILENO, "fatal signal: %s(%u)\n", strsignal(sig), sig);
	}
	unsigned skip = HC->flags & HAPPY_SKIP ? 3: 0;
	happycrash_backtrace(skip, 3);
	hc_print_anybye();
	exit(1);
}

void happycrash_panic(const char* format, ...){
	hc_break_condom();
	hc_print_welcome();
	hc_print_buildarg();
	if( ISTTY ){
		dprintf(STDERR_FILENO, FG(9)"panic"RESET() ": ");
	}
	else{
		dprintf(STDERR_FILENO, "panic: ");
	}
	va_list ap;
	va_start(ap, format);
	vdprintf(STDERR_FILENO, format, ap);
	va_end(ap);
	dprintf(STDERR_FILENO, "\n");
	unsigned skip = HC->flags & HAPPY_SKIP ? 2: 0;
	happycrash_backtrace(skip, -1);
	hc_print_anybye();
	exit(1);
}

void happycrash_begin(unsigned flags){
	ISTTY = isatty(STDERR_FILENO);
	if( flags ){
		SIGSTACK.ss_sp = SSTACK;
		SIGSTACK.ss_size  = HAPPYCRASH_STACK_SIZE;
		SIGSTACK.ss_flags = 0;
		if( sigaltstack(&SIGSTACK, NULL) < 0 ){
			fprintf(stderr, "signalstack error: %m\n");
			exit(1);
		}
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sa.sa_sigaction = happy_crash_time;
		sigemptyset(&sa.sa_mask);
		if( flags & HAPPY_SEGFAULT ){
			sigaction(SIGSEGV, &sa, NULL);
		}
	}
	hc_begin(flags);
}

