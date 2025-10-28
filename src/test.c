#include <happycrash.h>
#include <string.h>

void gen_message(const char* msg){
	happycrash_panic("on test %s", msg);
}

void gen_segfault(void){
	char** a = malloc(32);
	memset(a, 0, 2);
	for( unsigned i = 0; i < 1024; ++i ){
		a[i][0] = 1;
	}
	gen_message("wtf no segfault?");
}

int main(int argc, char** argv){
	int argi = 1;
	int panic = 0;
	int segfault = 0;
	int skip = 0;
	while( argi < argc ){
		if( !strcmp(argv[argi], "--panic") ){
			panic = 1;
		}
		else if( !strcmp(argv[argi], "--segfault") ){
			segfault = 1;
		}
		else if( !strcmp(argv[argi], "--skip") ){
			skip = 1;
		}
		++argi;
	}
	
	unsigned flags = 0;
	if( skip ){
		flags |= HAPPY_SKIP;
	}

	if( panic ){
		happycrash_begin(flags);
		gen_message("simple panic");
	}
	else if( segfault ){
		flags |= HAPPY_SEGFAULT;
		happycrash_begin(flags);
		gen_segfault();
	}
	return 0;
}





