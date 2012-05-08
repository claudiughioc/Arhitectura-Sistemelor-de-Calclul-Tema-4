#include <stdio.h>
#include <libmisc.h>
#include <spu_mfcio.h>
#include <string.h>


#define MAX         65000
#define waitag(t) mfc_write_tag_mask(1<<t); mfc_read_tag_status_all();


struct pixel {
	char red;
	char green;
	char blue;
};
unsigned long long spu_id;



static int calculate_manhattan(struct pixel *cand, struct pixel *vertical,
    int size)
{
    int distance = 0, i;
    for (i = 0; i < size; i++) {
        distance += abs(vertical[i].red - cand[i].red);
        distance += abs(vertical[i].green - cand[i].green);
        distance += abs(vertical[i].blue - cand[i].blue);
    }
    return distance;
}

int main(unsigned long long speid, unsigned long long argp,
	unsigned long long envp)
{
	printf("\tSPU %lld starts\n", argp);
    spu_id = argp;
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
        printf("\tSPU %lld got all the candidates for the first line from PPU\n", argp);



        /* Calculate the best candidate for this piece */
        int * distances = malloc_align(nr_candidati * sizeof(int), 4);
        int final_index, min_distance = MAX;
        for (i = 0; i < nr_candidati; i++) {
            distances[i] = calculate_manhattan(candidates[i],
                    vertical, piesa_h);
            if (distances[i] < min_distance) {
                min_distance = distances[i];
                final_index = i;
            }
        }
        printf("\tSPU %lld chose cand %d, dist %d\n", argp, final_index, min_distance);


        /* Wait for PPU to ask for results */
        int confirm;
        while (spu_stat_in_mbox() <= 0);
        confirm = spu_read_in_mbox();


        /* Send the results back to PPU */
        spu_write_out_intr_mbox(final_index);
        spu_write_out_intr_mbox(min_distance);

        /* Free memory for candidates */
        for (i = 0; i < nr_candidati; i++)
            free(candidates[i]);
        free(candidates);
        free(distances);
    }

	return 0;
}
