#include <stdio.h>
#include <stdlib.h>
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

struct dma_pixel {
	int red;
	int green;
	int blue;
    int aux;
};



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

static struct pixel * copy_from_bus(struct dma_pixel *bus, struct pixel *a, int piesa_h)
{
    printf("\t\t\t\tInitial in vector, pos 0 era %d\n",
           a[0].red);
    int i, aux;
    char c;
    for (i = 0; i < piesa_h; i++) {
        a[i].red = bus[i].red;
        a[i].green = bus[i].green;
        a[i].blue = bus[i].blue;
    }
    //printf("\t\t\t\t\tCOPY FROM BUS: bus %d, res: %d\n",
    //        bus[0].red, a[0].red);
    return a;
}

int main(unsigned long long speid, unsigned long long argp,
	unsigned long long envp)
{
	printf("\tSPU %lld starts\n", argp);
    spu_id = argp;
    unsigned int piesa_h, piesa_w, pointer_margin, nr_candidati, i,
                 piese_de_prelucrat, j;
    struct pixel *horizontal, *vertical;
    struct dma_pixel *bus;
    uint32_t last_tag;

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
    bus = malloc_align(piesa_h * sizeof(struct dma_pixel), 4);

    uint32_t tag_id = mfc_tag_reserve();
    if (tag_id == MFC_TAG_INVALID) {
        printf("\tSPU cannot allocate tag\n");
        return -1;
    }
    last_tag = tag_id;

    /*---------Processing the first line ------- */
    for (j = 0; j < piese_de_prelucrat; j++) {
        /* Get the first margin from PPU */
        while (spu_stat_in_mbox() <= 0);
        pointer_margin = spu_read_in_mbox();
        printf("\tSPU %lld marimea transferului DMA este %d\n", argp,
                piesa_h * sizeof(struct pixel));
        mfc_get((void *) bus, (void *)pointer_margin, (uint32_t) piesa_h *
                sizeof(struct dma_pixel), tag_id, 0, 0);
        vertical = copy_from_bus(bus, vertical, piesa_h);
        printf("\tSPU %lld got margin from PPU\n", argp);


        int response = (int)argp;
        spu_write_out_intr_mbox(response);


        /* Get the number of candidates for the first line */
        printf("\tSPU %lld waiting for the number of it's own candidates\n", argp);
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
                perror("Error on allocating candidate, first line");
                free(candidates);
                return -1;
            }

            while (spu_stat_in_mbox() <= 0);
            pointer_margin = spu_read_in_mbox();
            mfc_get((void *) bus, (void *)pointer_margin, 
                    (uint32_t) piesa_h * sizeof(struct dma_pixel), tag_id, 0, 0);
            candidates[i] = copy_from_bus(bus, candidates[i], piesa_h);
            if (i == 0 && argp == 0) {
                int t;
                for (t = 0; t < piesa_h; t++) {
                    printf("\t <<<<<SPU candidat primit:[%d] %d %d %d\n",
                            t, candidates[i][t].red, candidates[i][t].green,
                            candidates[i][t].blue);
                    printf("\t <<<<<BUS:[%d] %d %d %d\n",
                            t, bus[t].red, bus[t].green,
                            bus[t].blue);
                }
            }
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



    /* ----------Processing the first column ------------*/
    /* Wait for the number of pieces to solve */
    while (spu_stat_in_mbox() <= 0);
    piese_de_prelucrat = spu_read_in_mbox();
    printf("\tSPU %lld got number of pieces for column: %d\n",
            argp, piese_de_prelucrat);
    printf("\tSPU %lld last tag: %d\n", argp, last_tag);
    for (j = 0; j < piese_de_prelucrat; j++) {
        /* Get the first margin from PPU */
        while (spu_stat_in_mbox() <= 0);
        pointer_margin = spu_read_in_mbox();
        mfc_get((void *) bus, (void *)pointer_margin, 
                (uint32_t) piesa_h * sizeof(struct dma_pixel), last_tag, 0, 0);
        horizontal = copy_from_bus(bus, horizontal, piesa_h);
        printf("\tSPU %lld got margin from PPU\n", argp);


        /* Get the number of candidates for the first column */
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
                perror("Error on allocating candidate, first column");
                free(candidates);
                return -1;
            }

            while (spu_stat_in_mbox() <= 0);
            pointer_margin = spu_read_in_mbox();

            mfc_get((void *) bus, (void *)pointer_margin, 
                    (uint32_t) piesa_w * sizeof(struct dma_pixel), last_tag, 0, 0);
            candidates[i] = copy_from_bus(bus, candidates[i], piesa_h);

            int response = (int)argp;
            spu_write_out_intr_mbox(response);
        }
        printf("\tSPU %lld got all the candidates for the first line from PPU\n", argp);

        /* Calculate the best candidate for this piece ___COLUMN */
        int * distances = malloc_align(nr_candidati * sizeof(int), 4);
        int final_index, min_distance = MAX;
        for (i = 0; i < nr_candidati; i++) {
            distances[i] = calculate_manhattan(candidates[i],
                    horizontal, piesa_w);
            if (distances[i] < min_distance) {
                min_distance = distances[i];
                final_index = i;
            }
        }

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



    /* ------Calculating pieces for the rest of the puzzle ------*/
    while (spu_stat_in_mbox() <= 0);
    piese_de_prelucrat = spu_read_in_mbox();
    printf("\tSPU %lld got number of pieces for puzzle: %d\n",
            argp, piese_de_prelucrat);
    for (j = 0; j < piese_de_prelucrat; j++) {
        /* Get the left margin from PPU */
        while (spu_stat_in_mbox() <= 0);
        pointer_margin = spu_read_in_mbox();
        mfc_get((void *) bus, (void *)pointer_margin, 
                (uint32_t) piesa_h * sizeof(struct dma_pixel), last_tag, 0, 0);
        vertical = copy_from_bus(bus, vertical, piesa_h);
        int response = (int)argp;
        spu_write_out_intr_mbox(response);

        /* Get the top margin from PPU */
        while (spu_stat_in_mbox() <= 0);
        pointer_margin = spu_read_in_mbox();
        mfc_get((void *) bus, (void *)pointer_margin, 
                (uint32_t) piesa_h * sizeof(struct dma_pixel), last_tag, 0, 0);
        horizontal = copy_from_bus(bus, horizontal, piesa_h);
        response = (int)argp;
        spu_write_out_intr_mbox(response);


        /* Get the number of candidates for the rest of the puzzle */
        while (spu_stat_in_mbox() <= 0);
        nr_candidati = spu_read_in_mbox();
        printf("\tSPU %lld voi primi %d candidati\n", argp, nr_candidati);


        /* Get the horizontal and vertical candidates */
        struct pixel **h_candidates = malloc_align(nr_candidati *
                sizeof(struct pixel *), 4);
        if (!h_candidates) {
            perror("Error on allocating candidates");
            return -1;
        }
        struct pixel **v_candidates = malloc_align(nr_candidati *
                sizeof(struct pixel *), 4);
        if (!v_candidates) {
            perror("Error on allocating candidates, rest of puzzle");
            return -1;
        }
        for (i = 0; i < nr_candidati; i++) {
            h_candidates[i] = malloc_align(piesa_h * sizeof(struct pixel), 4);
            v_candidates[i] = malloc_align(piesa_h * sizeof(struct pixel), 4);
            if (!h_candidates[i] || !v_candidates[i]) {
                printf("\tSPU %lld nu poate aloca candidati\n", argp);
                perror("Error on allocating candidate");
                free(h_candidates);
                free(v_candidates);
                return -1;
            }
            /* Get the pointer to the horizontal top margin */
            while (spu_stat_in_mbox() <= 0);
            pointer_margin = spu_read_in_mbox();
            mfc_get((void *) bus, (void *)pointer_margin, 
                    (uint32_t) piesa_w * sizeof(struct dma_pixel), last_tag, 0, 0);
            h_candidates[i] = copy_from_bus(bus, h_candidates[i], piesa_h);
            int response = (int)argp;
            spu_write_out_intr_mbox(response);

            /* Get the pointer to the vertical left margin */
            while (spu_stat_in_mbox() <= 0);
            pointer_margin = spu_read_in_mbox();
            mfc_get((void *) bus, (void *)pointer_margin, 
                    (uint32_t) piesa_h * sizeof(struct dma_pixel), last_tag, 0, 0);
            v_candidates[i] = copy_from_bus(bus, v_candidates[i], piesa_h);
            spu_write_out_intr_mbox(response);
        }
        printf("\tSPU %lld got all the candidates for the rest of the puzzle from PPU\n", argp);

        /* Calculate the best candidate for this piece ___COLUMN */
        int * h_distances = malloc_align(nr_candidati * sizeof(int), 4);
        int * v_distances = malloc_align(nr_candidati * sizeof(int), 4);
        int final_index, min_distance = MAX;
        for (i = 0; i < nr_candidati; i++) {
            h_distances[i] = calculate_manhattan(h_candidates[i],
                    horizontal, piesa_w);
            v_distances[i] = calculate_manhattan(v_candidates[i],
                    vertical, piesa_h);
            if (h_distances[i] + v_distances[i] < min_distance) {
                min_distance = h_distances[i] + v_distances[i];
                final_index = i;
            }
        }

        /* Wait for PPU to ask for results */
        int confirm;
        while (spu_stat_in_mbox() <= 0);
        confirm = spu_read_in_mbox();

        /* Send the results back to PPU */
        spu_write_out_intr_mbox(final_index);
        spu_write_out_intr_mbox(min_distance);

        /* Free memory for candidates */
        for (i = 0; i < nr_candidati; i++) {
            free(h_candidates[i]);
            free(v_candidates[i]);
        }
        free(h_candidates);
        free(v_candidates);
        free(h_distances);
        free(v_distances);
        printf("\tSPU %lld sleeping for 5 sec\n", argp);
        sleep(5);
    }
    printf("\tSPU %lld a primit toate piesele si iese\n", argp);





	return 0;
}
