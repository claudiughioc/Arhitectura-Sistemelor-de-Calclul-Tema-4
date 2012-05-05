#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <libmisc.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define SPU_THREADS 	8
#define spu_mfc_ceil128(value)  ((value + 127) & ~127)
#define spu_mfc_ceil16(value)   ((value +  15) &  ~15)

volatile char str[256]  __attribute__ ((aligned(16)));
extern spe_program_handle_t spu_tema4;

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

int main(int argc, char **argv)
{
	spe_context_ptr_t ctxs[SPU_THREADS];
	pthread_t threads[SPU_THREADS];
	thread_arg_t arg[SPU_THREADS];
	spe_event_unit_t pevents[SPU_THREADS],
			 event_received;
	spe_event_handler_ptr_t event_handler;
	event_handler = spe_event_handler_create();
	struct pixel *a = NULL;
	struct pixel **piese = NULL;
	int width, height, max_color, i;


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
    printf("PPU am creat piesele\n");


	/* Print a test piece to an output file */
	printf("PPU writing the final image to output file\n");
	FILE *fout = fopen("test", "w");
	fprintf(fout, "P3\n");
	fprintf(fout, "%d %d\n%d\n", piesa_w, piesa_h, 255);
    struct pixel *piesa = piese[31];
	for (i = 0; i < piesa_w * piesa_h; i++) {
			fprintf(fout, "%d\n%d\n%d\n", piesa[i].red,
				piesa[i].green, piesa[i].blue);
	}
	close(fout);

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

	/* Print the final image to an output file */
	printf("PPU writing the final image to output file\n");
	fout = fopen(output_file, "w");
	fprintf(fout, "P3\n");
	fprintf(fout, "%d %d\n%d\n", width, height, 255);
	for (i = 0; i < width * height; i++) {
			fprintf(fout, "%d\n%d\n%d\n", a[i].red,
				a[i].green, a[i].blue);
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

