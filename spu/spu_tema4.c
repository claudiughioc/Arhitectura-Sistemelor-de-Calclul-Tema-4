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
	unsigned long long envp)
{
	printf("\tSPU %lld starts\n", argp);
    unsigned int piesa_h, piesa_w, pointer_margin, nr_candidati, i;
    struct pixel *horizontal, *vertical;

    /* Wait for initial parameters from PPU */
    while (spu_stat_in_mbox() <= 0);
    piesa_h = spu_read_in_mbox();
    while (spu_stat_in_mbox() <= 0);
    piesa_w = spu_read_in_mbox();
    printf("\tSPU %lld gets h %d, w %d\n", argp, piesa_h, piesa_w);

    /* Allocate memory for the comparison vectors */
    vertical = malloc_align(piesa_h * sizeof(struct pixel), 4);
    horizontal = malloc_align(piesa_w * sizeof(struct pixel), 4);

    /* Get the first margin from PPU */
    while (spu_stat_in_mbox() <= 0);
    pointer_margin = spu_read_in_mbox();
    uint32_t tag_id = mfc_tag_reserve();
    if (tag_id == MFC_TAG_INVALID) {
        printf("\tSPU cannot allocate tag\n");
        return -1;
    }
    mfc_get((void *) vertical, (void *)pointer_margin, (uint32_t) piesa_h *
            sizeof(struct pixel), tag_id, 0, 0);
    printf("\tSPU %lld got margin from PPU\n", argp);


    /* Get the number of candidates for the first line */
    while (spu_stat_in_mbox() <= 0);
    nr_candidati = spu_read_in_mbox();
    printf("\tSPU %lld voi primi %d candidati\n", argp, nr_candidati);



    /* Get the candidates from PPU */
    struct pixel **candidates = malloc_align(nr_candidati *
            sizeof(struct pixel *), 4);
    if (!candidates) {
        perror("Error on allocating candidates");
        return -1;
    }

    for (i = 0; i < nr_candidati; i++) {
        candidates[i] = malloc_align(piesa_h * sizeof(struct pixel), 4);
        if (!candidates[i]) {
            perror("Error on allocating candidate");
            free(candidates);
            return -1;
        }

        while (spu_stat_in_mbox() <= 0);
        pointer_margin = spu_read_in_mbox();
        uint32_t tag_id = mfc_tag_reserve();
        if (tag_id == MFC_TAG_INVALID) {
            printf("\tSPU cannot allocate tag\n");
            return -1;
        }
        mfc_get((void *) candidates[i], (void *)pointer_margin, 
                (uint32_t) piesa_h * sizeof(struct pixel), tag_id, 0, 0);
    }
    printf("\tSPU %lld got all the candidates for the first line from PPU\n");

    if (argp == 0) {
        printf("\t\tSPU %lld vertical:\n", argp);
        for (i = 0; i < piesa_h; i++)
            printf("\t\t%d\n", vertical[i].green);
        printf("\t\tSPU %lld candidat 0:\n", argp);
        for (i = 0; i < piesa_h; i++)
            printf("\t\t%d\n", candidates[0][i].green);

    }






	return 0;
}
