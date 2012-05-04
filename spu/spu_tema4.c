#include <stdio.h>
#include <libmisc.h>
#include <spu_mfcio.h>
#include <string.h>



#define waitag(t) mfc_write_tag_mask(1<<t); mfc_read_tag_status_all();


struct pixel {
	char red;
	char green;
	char blue;
};


int main(unsigned long long speid, unsigned long long argp,
	unsigned long long envp){
	printf("SPU %lld starts\n", argp);


	return 0;
}
