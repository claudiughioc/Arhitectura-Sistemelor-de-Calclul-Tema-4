#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <libmisc.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define SPU_THREADS 	8
#define LAST            0x01
#define FIRST           0x02
#define SOLVED          (-10)
#define MAX             65000
#define spu_mfc_ceil128(value)  ((value + 127) & ~127)
#define spu_mfc_ceil16(value)   ((value +  15) &  ~15)

volatile char str[256]  __attribute__ ((aligned(16)));
extern spe_program_handle_t spu_tema4;
int piesa_curenta;

typedef struct {
	int cellno;
	int data;
	spe_context_ptr_t spe;
} thread_arg_t;

struct pixel {
	char red;
	char green;
	char blue;
};

struct spu_response {
    int index;
    int spu;
    int distance;
};

void *ppu_pthread_function(void *thread_arg) {
	thread_arg_t *arg = (thread_arg_t *) thread_arg;
	unsigned int entry = SPE_DEFAULT_ENTRY;
	if (spe_context_run(arg->spe, &entry, 0, (void *) arg->cellno, 
				(void *) arg->data, NULL) < 0) {
		perror ("Failed runnnning context");
		exit (1);
	}

	pthread_exit(NULL);
}

void read_from_file(FILE *fin, struct pixel **a, int *width, int *height,
		int *max_color)
{
	printf("PPU reading from file\n");
	char line[256];
	char *numbers, *tok;
	long line_no = 0, i = 0;
	int red, green, blue;
	
	/* Check if the file is ppm */
	fgets(line, sizeof(line), fin);
	if (strncmp(line, "P3", 2)) {
		perror("The input file is not ppm");
		return;
	}

	/* Read initial parameters */
	fscanf(fin, "%d", width);
	fscanf(fin, "%d", height);
	fscanf(fin, "%d", max_color);
	printf("PPU reads %d, %d, %d\n", *width, *height, *max_color);
	*a = malloc_align(*width * *height * sizeof(struct pixel), 4);
	if (!(*a)) {
		perror("Error on allocating memory for image");
		return;
	}

	/* Read the pixels */
	while(fscanf(fin, "%d %d %d", &red, &green, &blue) != EOF){
		(*a)[i].red = red;
		(*a)[i].green = green;
		(*a)[i].blue = blue;
		i++;
	}
}

struct pixel ** build_pieces(struct pixel *a, int nr_piese, int piesa_h,
        int piesa_w, int width, int height)
{
	struct pixel **piese = malloc_align(nr_piese * sizeof(struct pixel *), 4);
	if (!piese) {
		perror("Error on allocating pieces");
		return NULL;
	}

	int i = 0, linie = 0, coloana = 0, j, k, source, dest;
	for (i = 0; i < nr_piese; i++) {
		piese[i] = malloc_align(piesa_h * piesa_w * sizeof(struct pixel), 4);
		if (!piese[i]) {
			perror("Error on allocatin one piece");
			free(piese);
			return NULL;
		}
        for (j = 0; j < piesa_h; j++) {
            for (k = 0; k < piesa_w; k++) {
                source = (linie + j) * width + coloana + k;
                dest = j * piesa_w + k;
                piese[i][dest] = a[source];
            }
        }
        coloana += piesa_w;
        if (coloana >= width) {
            coloana = 0;
            linie += piesa_h;
        }
	}
    return piese;
}


void place_piece_on_puzzle(struct pixel **final, struct pixel *piesa,
        int piesa_h, int piesa_w, int width, int height, int position)
{
    //printf("PPU pune piesa la pozitia %d\n", piesa_curenta);
    int i, j, k, source, dest;
    for (i = 0; i < piesa_h; i++) {
        for (j = 0; j < piesa_w; j++) {
            source = i * piesa_w + j;
            dest = (((position * piesa_w) / width) * piesa_h + i)
                * width + ((position * piesa_w) % width) + j;
            (*final)[dest] = piesa[source];
        }
    }
    piesa_curenta++;
}


/* Builds a vector with the vertical extremity of a piece indicated by col*/
struct pixel * get_vertical_side(struct pixel *piece, int piesa_h,
        int piesa_w, int column)
{
    struct pixel *array = malloc_align(piesa_h * sizeof(struct pixel), 4);
    if (!array) {
        perror("Error on allocating extremity");
        return NULL;
    }
    int i;
    for (i = 0; i < piesa_h; i++) {
        if (column == LAST)
            array[i] = piece[(i + 1) * piesa_w - 1];
        else
            array[i] = piece[i * piesa_w];
    }
    return array;
}

int main(int argc, char **argv)
{
	spe_context_ptr_t ctxs[SPU_THREADS];
	pthread_t threads[SPU_THREADS];
	thread_arg_t arg[SPU_THREADS];
	spe_event_unit_t pevents[SPU_THREADS],
			 event_received;
	spe_event_handler_ptr_t event_handler;
	event_handler = spe_event_handler_create();
	struct pixel *a = NULL, *final = NULL;
	struct pixel **piese = NULL, **solved_puzzle;
    struct spu_response resp[SPU_THREADS];
	int width, height, max_color, i, nevents;


	printf("\n\n\n\n-----------PPU------------\n");
	/* Store the arguments */
	if (argc < 5)
		perror("./program input_file nr_piese piesa_h piesa_w");
	char *input_file = argv[1];
	int nr_piese = atoi(argv[2]);
	int piesa_h = atoi(argv[3]);
	int piesa_w = atoi(argv[4]);
	char *output_file = "output.ppm";
	printf("Citesc din %s si scriu in %s\n", input_file, output_file);
	printf("PPU: piese %d, dim %d x %d\n", nr_piese, piesa_h, piesa_w);

	/* Read the matrix */
	FILE *fin = fopen(input_file, "r");
	if (!fin)
		perror("Error while opening input file for reading");
	read_from_file(fin, &a, &width, &height, &max_color);



	/* Create the pieces */
	piese = build_pieces(a, nr_piese, piesa_h, piesa_w, width, height);
    /* Allocate memory for the zolved puzzle */
    solved_puzzle = malloc_align(nr_piese * sizeof(struct pixel *), 4);
    if (!solved_puzzle) {
        perror("Error on allocating solved puzzle");
        return -1;
    }
    for (i = 0; i < nr_piese; i++) {
        solved_puzzle[i] = malloc_align(piesa_h * piesa_w *
                sizeof(struct pixel), 4);
        if (!solved_puzzle[i]) {
            perror("Error on allocating solved piece");
            free(solved_puzzle);
            return -1;
        }
    }
    solved_puzzle[0] = piese[0];
    piesa_curenta = 0;
    /* Allocate memory for the final image */
    final = malloc_align(width * height * sizeof(struct pixel), 4);
    if (!final) {
        perror("Error on alocating final image");
        return -1;
    }
    /* Place the first piece on the final image */
    place_piece_on_puzzle(&final, piese[0], piesa_h, piesa_w, 
            width, height, 0);

    printf("PPU am creat piesele\n");


	/* Create several SPE-threads to execute 'SPU'. */
	for (i = 0; i < SPU_THREADS; i++) {
		/* Create context */
		if ((ctxs[i] = spe_context_create (SPE_EVENTS_ENABLE, NULL)) == NULL) {
			perror ("Failed creating context");
			exit (1);
		}

		/* Load program into context */
		if (spe_program_load (ctxs[i], &spu_tema4)) {
			perror ("Failed loading program");
			exit (1);
		}

		/* Send some more paramters to the SPU. */
		arg[i].cellno = i;
		arg[i].spe = ctxs[i];
		arg[i].data = 55 + i;

		/* Create thread for each SPE context */
		if (pthread_create (&threads[i], NULL, &ppu_pthread_function, &arg[i])) {
			perror ("Failed creating thread");
			exit (1);
		}
		/* The type of the event(s) we are working with */
		pevents[i].events = SPE_EVENT_OUT_INTR_MBOX;
		pevents[i].spe = ctxs[i];
		pevents[i].data.u32 = i; // just some data to pass

		spe_event_handler_register(event_handler, &pevents[i]);
	}


    /* Send initial parameters to SPUs */
    printf("PPU sends initial parameters to SPU\n");
    int piese_de_prelucrat = width / piesa_w - 1;
    for (i = 0; i < SPU_THREADS; i++) {
        spe_in_mbox_write(ctxs[i], (void *) &piesa_h, 1, 
           SPE_MBOX_ANY_NONBLOCKING);
        spe_in_mbox_write(ctxs[i], (void *) &piesa_w, 1, 
           SPE_MBOX_ANY_NONBLOCKING);
        spe_in_mbox_write(ctxs[i], (void *) &piese_de_prelucrat, 1, 
           SPE_MBOX_ANY_NONBLOCKING);

    }

    /* Vector of pieces best scores */
    int *best_pieces = malloc_align(nr_piese * sizeof(int), 4);
    best_pieces[0] = SOLVED;
    int nr_candidati, j, spu_qty, k, index_candidate;
    int factor, index, win;
    struct pixel *curr_piece = solved_puzzle[0], *candidate;
    /* The current extremity of a piece */
    struct pixel *latura_test, *latura_candidat;

    /* Determine best pieces for the first line */
    printf("PPU extracts vertical extremities to send to SPU\n");
    nr_candidati = 31;
    for (i = 0; i < piese_de_prelucrat; i++) {
        latura_test = get_vertical_side(curr_piece, piesa_h, piesa_w, LAST);
        index_candidate = 0;
        for (j = 0; j < SPU_THREADS; j++) {
            /* Send the margin of the current piece to each SPU */
            printf("PPU will send pointer to vertical margin to %d,\
                    pointer %d\n", j, latura_test);
            int pointer_margin = latura_test;
            spe_in_mbox_write(ctxs[j], (void *) &pointer_margin,
                    1, SPE_MBOX_ANY_NONBLOCKING);

            /* Calculate how many pieces will be sent to each SPU */
            spu_qty = (nr_candidati - (nr_candidati % (SPU_THREADS - 1)))
                    / (SPU_THREADS - 1);
            factor = spu_qty;
            if (j == SPU_THREADS - 1)
                spu_qty = nr_candidati % (SPU_THREADS - 1);
            printf("PPU QUANTITY: %d\n", spu_qty);
            spe_in_mbox_write(ctxs[j], (void *) &spu_qty, 1,
                    SPE_MBOX_ANY_NONBLOCKING);

            /* Send the appropriate candidates to SPU to check */
            for (k = 0; k < spu_qty; k++) {
                if (best_pieces[index_candidate] == SOLVED) {
                    k--;
                    index_candidate++;
                    continue;
                }
                printf("PPU will send to %d, piece %d\n", j, index_candidate);
                candidate = piese[index_candidate];
                best_pieces[index_candidate] = -1 * j;
                latura_candidat = get_vertical_side(candidate, piesa_h,
                        piesa_w, FIRST);
                pointer_margin = latura_candidat;
                spe_in_mbox_write(ctxs[j], (void *) &pointer_margin, 
                        1, SPE_MBOX_ANY_NONBLOCKING);                

                /* Wait for confirmation from SPUs */
                nevents = spe_event_wait(event_handler, &event_received, 1, -1);
                if (nevents <= 0) {
                    //FIXME:ai belit pula
                }
                int response;
                while(spe_out_intr_mbox_status(event_received.spe) < 1);
                spe_out_intr_mbox_read(event_received.spe, &response, 1,
                        SPE_MBOX_ANY_NONBLOCKING);
                printf("PPU am primit confirmare de la SPU %d\n", response);

                index_candidate++;
            }
        }

        /* Ask for results from each SPU */
        for (j = 0; j < SPU_THREADS; j++)
            spe_in_mbox_write(ctxs[j], (void *) &j, 1, 
                    SPE_MBOX_ANY_NONBLOCKING);
        printf("PPU asked for results from all the spu\n");

        /* Get the result for the current piece from SPUs */
        for (j = 0; j < SPU_THREADS; j++)
            resp[j].index = -1;
        for (j = 0; j < 2 * SPU_THREADS; j++)
        {
            printf("PPU intru sa primesc rezultat\n");
            int data;
            spe_event_wait(event_handler, &event_received, 1, -1);
            while (spe_out_intr_mbox_status(event_received.spe) < 1);
            spe_out_intr_mbox_read(event_received.spe, (&data), 1,
                    SPE_MBOX_ANY_NONBLOCKING);
            int SPU_id = event_received.data.u32;
            resp[SPU_id].spu = SPU_id;
            if (resp[SPU_id].index == -1)
                resp[SPU_id].index = data;
            else
                resp[SPU_id].distance = data;
            printf("PPU got final response from SPU %d, data %d\n",
                    SPU_id, data);
        }


        /* Determine the best piece */
        struct spu_response best_response;
        best_response.distance = MAX;
        for (j = 0; j < SPU_THREADS; j++) {
            printf("PPU resp %d: %d, %d\n", j, resp[j].index,
                    resp[j].distance);
            if (resp[j].distance < best_response.distance)
                best_response = resp[j];
        }
        printf("PPU BEST PIECE: spu: %d, index: %d, distance %d\n",
                best_response.spu, best_response.index, best_response.distance);
        for (j = 0; j < nr_piese; j++) {
            if (best_pieces[j] == -1 * best_response.spu) {
                index = 0;
                for (k = 0; k <= best_response.index; k++) {
                    if (best_pieces[j + index] == SOLVED) {
                        k--;
                        index++;
                        continue;
                    }
                    index++;
                }
                win = index + j - 1;
                break;
            }
        }
        printf("PPU winning piece %d\n", win);
        best_pieces[win] = SOLVED;
        solved_puzzle[i +1] = piese[win];
        place_piece_on_puzzle(&final, piese[win], piesa_h, piesa_w,
                width, height, i + 1);

        /* Go to the next piece */
        curr_piece = solved_puzzle[i + 1];
        nr_candidati--;
        printf("\n\n\n___________PPU goes to next piece, choosing \
                from %d candidates________\n", nr_candidati);
    }



    /* Determine the best pieces for the first column */


	/* Print the final image to an output file */
	printf("PPU writing the final image to output file\n");
	FILE *fout = fopen(output_file, "w");
	fprintf(fout, "P3\n");
	fprintf(fout, "%d %d\n%d\n", width, height, 255);
	for (i = 0; i < width * height; i++) {
			fprintf(fout, "%d\n%d\n%d\n", final[i].red,
				final[i].green, final[i].blue);
	}
	close(fout);

	/* Wait for SPU-thread to complete execution. */
	for (i = 0; i < SPU_THREADS; i++) {
		if (pthread_join(threads[i], NULL)) {
			perror("Failed pthread_join");
			exit(1);
		}

		/* Destroy context */
		if (spe_context_destroy(ctxs[i]) != 0) {
			perror("Failed destroying context");
			exit(1);
		}
	}

	printf("\nThe program has successfully executed.\n");
	return 0;
}

