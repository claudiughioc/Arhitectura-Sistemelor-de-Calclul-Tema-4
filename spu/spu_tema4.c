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
    unsigned int piesa_h, piesa_w, pointer_margin, nr_candidati, i,
                 piese_de_prelucrat, j;
    struct pixel *horizontal, *vertical;

    /* Wait for initial parameters from PPU */
    while (spu_stat_in_mbox() <= 0);
    piesa_h = spu_read_in_mbox();
    while (spu_stat_in_mbox() <= 0);
    piesa_w = spu_read_in_mbox();
    while (spu_stat_in_mbox() <= 0);
    piese_de_prelucrat = spu_read_in_mbox();
    printf("\tSPU %lld gets h %d, w %d, nr %d\n", argp, piesa_h,
            piesa_w, piese_de_prelucrat);

    /* Allocate memory for the comparison vectors */
    vertical = malloc_align(piesa_h * sizeof(struct pixel), 4);
    horizontal = malloc_align(piesa_w * sizeof(struct pixel), 4);


    for (j = 0; j < piese_de_prelucrat; j++) {
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
            int response = (int)argp;
            spu_write_out_intr_mbox(response);
        }
        printf("\tSPU %lld got all the candidates for the first line from PPU\n");




        /* Free memory for candidates */
        for (i = 0; i < nr_candidati; i++)
            free(candidates[i]);
        free(candidates);
    }

	return 0;
}
