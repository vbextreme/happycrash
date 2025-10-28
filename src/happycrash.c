#define _GNU_SOURCE
#include <happycrash.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <elfutils/libdwfl.h>
#include <elfutils/libdw.h>
#include <elf.h>

#define TBL_SIZE 32
#define TMP_DIR "/tmp"
#define TMP_NAME "happycrash."

static FILE* tmp_open(char path[PATH_MAX]){
	FILE* f = NULL;
	static const char map[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVXYZ0123456789";
	const unsigned nn = sprintf(path, "%s/%s", TMP_DIR, TMP_NAME);
	unsigned k = 8;
	while( 1 ){
		if( nn+k+1 >= PATH_MAX ){
			fprintf(stderr, "unable to generate tempfile");
			exit(1);
		}
		for( unsigned i = 0; i < k; ++i ){
			path[nn+i] = map[rand() % strlen(map)];
		}
		path[nn+k] = 0;
		f = fopen(path, "wx+");
		if( f ) return f;
		++k;
	}
}

static void cp(FILE* dst, FILE* src){
	char buf[4096*2];
	ssize_t nr;
	while( (nr=fread(buf, 1, 4096*2, src)) > 0 ){
		ssize_t wr = fwrite(buf, 1, nr, dst);
		if( wr != nr ){
			fprintf(stderr, "error on cp\n");
			exit(1);
		}
	}
	if( nr < 0 ){
		fprintf(stderr, "error on cp\n");
		exit(1);
	}
}

static hcTbl_s* tbl_ctor(hcTbl_s* tbl){
	tbl->count = 0;
	tbl->size  = TBL_SIZE;
	tbl->tbl   = malloc(sizeof(char*) * tbl->size);
	return tbl;
}

static unsigned tbl_add(hcTbl_s* tbl, const char* name){
	if( !name ){
		fprintf(stderr, "unable to add NULL name\n");
		exit(1);
	}
	if( tbl->count >= tbl->size ){
		tbl->size *= 2;
		tbl->tbl = realloc(tbl->tbl, tbl->size * sizeof(char*));
		if( !tbl->tbl ){
			fprintf(stderr, "memory error: %m\n");
			exit(1);
		}
	}
	unsigned i  = tbl->count++;
	size_t len  = strlen(name);
	tbl->tbl[i] = malloc(len+1);
	memcpy(tbl->tbl[i], name, len+1);
	return i;
}

static long tbl_find(hcTbl_s* tbl, const char* name){
	for( unsigned i = 0; i < tbl->count; ++i ){
		if( !strcmp(name, tbl->tbl[i]) ) return i;
	}
	return -1;
}

static size_t addr_add(hcDB_s* db, uintptr_t addr, unsigned file, unsigned fn, unsigned loc){
	if( db->countAddr >= db->sizeAddr ){
		db->sizeAddr *= 2;
		db->addr = realloc(db->addr, sizeof(mapAddr_s) * db->sizeAddr);
		if( !db->addr ){
			fprintf(stderr, "memory error: %m\n");
			exit(1);
		}
	}
	size_t i = db->countAddr++;
	db->addr[i].start = addr;
	db->addr[i].size  = 1;
	db->addr[i].file  = file;
	db->addr[i].fn    = fn;
	db->addr[i].loc   = loc;
	return i;
}

static unsigned rnum(const char** str){
	char* en = NULL;
	errno = 0;
	uintptr_t ret = strtoul(*str, &en, 10);
	if( !en || errno ){
		fprintf(stderr, "invalid number<%s>: %m\n", *str);
		exit(1);
	}
	*str = en;
	return ret;
}

static hcDB_s* db_ctor(hcDB_s* db, const char* version){
	tbl_ctor(&db->file);
	tbl_ctor(&db->fn);
	db->sizeAddr  = 4096;
	db->countAddr = 0;
	db->addr = malloc(sizeof(mapAddr_s) * db->sizeAddr);
	tbl_add(&db->file, "??");
	tbl_add(&db->fn, "??");
	addr_add(db, 0, 0, 0, 0);
	db->version[0] = db->version[1] = db->version[2] = 0;
	if( version ){
		unsigned iv = 0;
		do{
			db->version[iv++] = rnum(&version);
			if( *version ) ++version;
		}while( iv < 3 && *version );
	}
	return db;
}

static void* elf_read_section(int fd, size_t offset, size_t size, int str){
	char* sec = malloc(size + str);
	if( lseek(fd, offset, SEEK_SET) < 0 ){
		fprintf(stderr, "unable to seek elf: %m\n");
		exit(1);
	}
	if( read(fd, sec, size) != (ssize_t)size ){
		fprintf(stderr, "unable to read elf section: %m\n");
		exit(1);
	}
	if( str ) sec[size] = '\0';
	return sec;
}

static void elf_text_size(const char* path, uintptr_t* addr, size_t* size, char** buildarg){
	int fd = open(path, O_RDONLY);
	if (fd < 0){ 
		fprintf(stderr, "on open %s: %m\n", path);
		exit(1);
	}
	Elf64_Ehdr ehdr;
	if( read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr) ){
		fprintf(stderr, "unable read header:%m\n");
		exit(1);
	}
	if( memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ){
		fprintf(stderr, "is not ELF\n");
		exit(1);
	}
	Elf64_Shdr *shdrs = malloc(ehdr.e_shentsize * ehdr.e_shnum);
	if( lseek(fd, ehdr.e_shoff, SEEK_SET) < 0 ){
		fprintf(stderr, "unable to seek: %m\n");
		exit(1);
   	}
	if( read(fd, shdrs, ehdr.e_shentsize * ehdr.e_shnum) != ehdr.e_shentsize * ehdr.e_shnum ){
		fprintf(stderr, "unable to read section: %m\n");
		exit(1);
	}
	Elf64_Shdr sh_strtab = shdrs[ehdr.e_shstrndx];
	char *shstrtab = malloc(sh_strtab.sh_size);
	if( lseek(fd, sh_strtab.sh_offset, SEEK_SET) < 0 ){
		fprintf(stderr, "unable to seek table name:%m\n");
		exit(1);
	}
	if( read(fd, shstrtab, sh_strtab.sh_size) != (ssize_t)sh_strtab.sh_size ){
		fprintf(stderr, "unable read table name\n");
		exit(1);
	}
	*addr = 0;
	*size = 0;
	for( long i = 0; i < ehdr.e_shnum; ++i ){
		const char *name = shstrtab + shdrs[i].sh_name;
		if( !strcmp(name, ".text") ){
			*addr = shdrs[i].sh_addr;
			*size = shdrs[i].sh_size;
		}
		else if( !strcmp(name, ".GCC.command.line") ){
			*buildarg = elf_read_section(fd, shdrs[i].sh_offset, shdrs[i].sh_size, 1);
		}
	}
	if( *addr == 0 && *size == 0 ){
		fprintf(stderr, "unable to find section .text\n");
		exit(1);
	}
	free(shdrs);
	free(shstrtab);
	close(fd);
}

Dwfl_Callbacks dwflcallbacks = {
	.find_elf = dwfl_build_id_find_elf,
	.find_debuginfo = dwfl_build_id_find_debuginfo
};

static Dwfl* open_dwfl(const char* path){
	Dwfl *dwfl = dwfl_begin(&dwflcallbacks);
	if (!dwfl) {
		fprintf(stderr, "dwfl_begin failed\n");
		exit(1);
	}
	dwfl_report_begin(dwfl);
	Dwfl_Module *mod = dwfl_report_elf(dwfl, path, path, -1, 0, false);
	if (!mod) {
		fprintf(stderr, "dwfl unable to open ELF: %s\n", path);
		exit(1);
	}
	if (dwfl_report_end(dwfl, NULL, NULL) != 0) {
		fprintf(stderr, "dwfl_report_end failed\n");
		exit(1);
	}
	return dwfl;
}

static void close_dwfl(Dwfl* dw){
	dwfl_end(dw);
}

static int dwarf_addr_to_info(Dwfl* dwfl, Dwarf_Addr a, const char** fname, const char** fn, size_t* loc){
	Dwfl_Module *mod = dwfl_addrmodule(dwfl, a);
	if( !mod ) return -1;
	*fn = dwfl_module_addrname(mod, a);
	if( !*fn ) *fn = "??";
	Dwfl_Line *dwline = dwfl_module_getsrc(mod, a);
	if( !dwline ){
		*loc = 0;
		*fname = "??";
		return 0;
	}
	int linenr = 0;
	*fname = dwfl_lineinfo(dwline, NULL, &linenr, NULL, NULL, NULL);
	if( !*fname ){
		*fname = "??";
	}
	else{
		const char* norm = *fname;
		while( *norm == '.' ) ++norm;
		while( *norm == '/' ) ++norm;
		*fname = norm;
	}
	*loc = linenr;
	return 0;
}

static hcDB_s* db_load(hcDB_s* db, const char* path, const char* version){
	db_ctor(db, version);
	uintptr_t addr;
	size_t size;
	char* buildarg = NULL;
	elf_text_size(path, &addr, &size, &buildarg);
	if( buildarg ){
		db->buildarg = buildarg;
	}
	else{
		db->buildarg = "no build arg";
	}
	Dwfl* dwfl = open_dwfl(path);
	size_t la = 0;
	const char* lastfile = "";
	const char* lastfn   = "";
	unsigned    idfile   = 0;
	unsigned    idfn     = 0;
	
	for( Dwarf_Addr i = addr; i < addr + size; ++i ){
		const char* fname;
		const char* fn;
		size_t loc;
		if( !dwarf_addr_to_info(dwfl, i, &fname, &fn, &loc) ){
			if( strcmp(lastfile, fname) ){
				long id = tbl_find(&db->file, fname);
				if( id == -1 ) id = tbl_add(&db->file, fname);
				idfile   = id;
				lastfile = fname;
			}
			if( strcmp(lastfn, fn) ){
				long id = tbl_find(&db->fn, fn);
				if( id == -1 ) id = tbl_add(&db->fn, fn);
				idfn   = id;
				lastfn = fn;
			}
			if( db->addr[la].file != idfile || db->addr[la].fn != idfn || db->addr[la].loc != loc ){
				db->addr[la].size = i - db->addr[la].start;
				/*printf("0x%lx-%lu %s(%u):%s(%u)%lu\n",
					db->addr[la].start,
					db->addr[la].size,
					db->file.tbl[db->addr[la].file],
					db->addr[la].file,
					db->fn.tbl[db->addr[la].fn],
					db->addr[la].fn,
					db->addr[la].loc
				);*/
				la = addr_add(db, i, idfile, idfn, loc);
			}
		}
	}
	db->addr[la].size = (addr+size) - db->addr[la].start;
	/*printf("0x%lx-%lu %s(%u):%s(%u)%lu\n",
		db->addr[la].start,
		db->addr[la].size,
		db->file.tbl[db->addr[la].file],
		db->addr[la].file,
		db->fn.tbl[db->addr[la].fn],
		db->addr[la].fn,
		db->addr[la].loc
	);
	printf("last la: %lu count: %lu\n", la, db->countAddr);
	*/

	close_dwfl(dwfl);
	if( db->countAddr < 2 ){
		fprintf(stderr, "not find many address\n");
		exit(1);
	}
	return db;
}

static void wnb(FILE* f, uint64_t value){
	uint8_t tmp[8];
	unsigned is = 7;
	do{
		tmp[is--] = value & 0x7F;
		value >>= 7;
	}while( value );
	++is;
	for( unsigned i = is; i < 7; ++i ){
		tmp[i] |= 0x80;
	}
	unsigned count = 8-is;
	if( fwrite(&tmp[is], 1, count, f) != count ){
		fprintf(stderr, "unable to write on temp file: %m\n");
		exit(1);
	}
}

static void wu16(FILE* f, uint16_t val){
	if( fwrite(&val, 1, 2, f) < 2 ){
		fprintf(stderr, "unable to write on temp file: %m\n");
		exit(1);
	}
}

static void wstr(FILE* f, const char* str){
	size_t len = strlen(str);
	if( fwrite(str, 1, len+1, f) < len+1 ){
		fprintf(stderr, "unable to write on temp file: %m\n");
		exit(1);
	}
}

static void db_save(hcDB_s* db, const char* path){
	size_t minimalSize = ((db->file.count + db->fn.count) * 8) + sizeof(hcDB_s);
	FILE* f = fopen(path, "w");
	char buf[4096];
	setvbuf(f, buf, _IOFBF, 4096);
	wu16(f, HAPPYCRASH_MAGIC);
	wnb(f, db->version[0]);
	wnb(f, db->version[1]);
	wnb(f, db->version[2]);
	wstr(f, db->buildarg);
	wnb(f, db->file.count);
	for( unsigned i = 0; i < db->file.count; ++i ){
		wstr(f, db->file.tbl[i]);
	}
	wnb(f, db->fn.count);
	for( unsigned i = 0; i < db->fn.count; ++i ){
		wstr(f, db->fn.tbl[i]);
	}
	wnb(f, db->addr[1].start);
	wnb(f, db->countAddr);
	uintptr_t prev = db->addr[1].start;
	for( unsigned i = 1; i < db->countAddr; ++i ){
		wnb(f, db->addr[i].start-prev);
		wnb(f, db->addr[i].size);
		wnb(f, db->addr[i].file);
		wnb(f, db->addr[i].fn);
		wnb(f, db->addr[i].loc);
		prev = db->addr[i].start;
	}
	long sz = ftell(f);
	if( sz < 0 ){
		fprintf(stderr, "unable to find file size: %m\n");
		exit(1);
	}
	if( sz < (long)minimalSize ){
		minimalSize -= sz;
		char* zero = calloc(minimalSize, 1);
		if( fwrite(zero, 1, minimalSize, f) < minimalSize ){
			fprintf(stderr, "error on fill 0 for minimal size: %m\n");
			exit(1);
		}
		free(zero);
	}
	fclose(f);
}

int main(int argc, char** argv){
	srand(time(NULL));
	const char* elf = NULL;
	const char* out = NULL;
	const char* version = NULL;
	int strip = 0;
	char tmpDB[PATH_MAX];
	FILE* fdb = tmp_open(tmpDB);
	char tmpStrip[PATH_MAX];
	FILE* fstrip = tmp_open(tmpStrip);
	
	int argi = 1;
	while( argi < argc ){
		if( !strcmp(argv[argi], "-e") ){
			elf = argv[++argi];
		}
		else if( !strcmp(argv[argi], "-o") ){
			out = argv[++argi];
		}
		else if( !strcmp(argv[argi], "-s") ){
			strip = 1;
		}
		else if( !strcmp(argv[argi], "--version") ){
			version = argv[++argi];
		}
		++argi;
	}
	if( elf == NULL ){
		fprintf(stderr, "aspected -e <elf>\n");
		exit(1);
	}
	hcDB_s db;
	db_load(&db, elf, version);
	db_save(&db, tmpDB);
	if( strip ){
		char* cmd = NULL;
		asprintf(&cmd, "strip -o %s --strip-all %s", tmpStrip, elf);
		system(cmd);
		free(cmd);
	}
	else{
		FILE* f = fopen(elf, "r");
		if( !f ){
			fprintf(stderr, "error on open elf file: %m\n");
			exit(1);
		}
		if( setvbuf(f, NULL, _IONBF, 0) ){
			fprintf(stderr, "error on unbuffered elf: %m\n");
			exit(1);
		}
		cp(fstrip, f);
		fclose(f);
	}
	if( out ){
		char* cmd = NULL;
		asprintf(&cmd, "objcopy --add-section %s=%s --set-section-flags %s=%s %s %s",
			HAPPYCRASH_SECTION,
			tmpDB,
			HAPPYCRASH_SECTION,
			HAPPYCRASH_SECTION_FLAGS,
			tmpStrip,
			out
		);
		system(cmd);
		free(cmd);
		unlink(tmpDB);
	}
	
	unlink(tmpStrip);
	fclose(fstrip);
	fclose(fdb);
	return 0;
}

