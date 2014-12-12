#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <complex.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>

#include "tsp-types.h"
#include "tsp-job.h"
#include "tsp-genmap.h"
#include "tsp-print.h"
#include "tsp-tsp.h"


/* macro de mesure de temps, retourne une valeur en nanosecondes */
#define TIME_DIFF(t1, t2) \
        ((t2.tv_sec - t1.tv_sec) * 1000000000ll + (long long int) (t2.tv_nsec - t1.tv_nsec))


/* tableau des distances */
tsp_distance_matrix_t distance ={};

/** Paramètres **/

/* nombre de villes */
int nb_towns=10;
/* graine */
long int myseed= 0;
/* nombre de threads */
int nb_threads=1;

/* affichage SVG */
bool affiche_sol= false;


// Ajout d'une structure pour passer en parametres les arguments de tsp_thread
struct tsp_args{
        int hops;
        int len;
        tsp_path_t* path;
        long long int *cuts;
        tsp_path_t* sol;
        int *sol_len;
        struct tsp_queue *q;
};

// Thread qui appelle tsp.
static void* tsp_thread(void* params)
{

        struct tsp_args *args = (struct tsp_args*) params;
        while (!empty_queue (args->q)) {
                get_job (args->q, *(args->path), &args->hops, &args->len);
                tsp(args->hops, args->len, *(args->path), args->cuts, *(args->sol), args->sol_len);
        }
        pthread_exit(NULL);	
}

static void generate_tsp_jobs (struct tsp_queue *q, int hops, int len, tsp_path_t path, long long int *cuts, tsp_path_t sol, int *sol_len, int depth)
{
        if (len >= minimum) {
                (*cuts)++ ;
                return;
        }

        if (hops == depth) {
                /* On enregistre du travail à faire plus tard... */
                add_job (q, path, hops, len);
        } else {
                int me = path [hops - 1];        
                for (int i = 0; i < nb_towns; i++) {
                        if (!present (i, hops, path)) {
                                path[hops] = i;
                                int dist = distance[me][i];
                                generate_tsp_jobs (q, hops + 1, len + dist, path, cuts, sol, sol_len, depth);
                        }
                }
        }
}

static void usage(const char *name) {
        fprintf (stderr, "Usage: %s [-s] <ncities> <seed> <nthreads>\n", name);
        exit (-1);
}

int main (int argc, char **argv)
{
        unsigned long long perf;
        tsp_path_t path;
        tsp_path_t sol;
        int sol_len;
        long long int cuts = 0;
        struct tsp_queue q;
        struct timespec t1, t2;

        /* lire les arguments */
        int opt;
        while ((opt = getopt(argc, argv, "s")) != -1) {
                switch (opt) {
                        case 's':
                                affiche_sol = true;
                                break;
                        default:
                                usage(argv[0]);
                                break;
                }
        }

        if (optind != argc-3)
                usage(argv[0]);

        nb_towns = atoi(argv[optind]);
        myseed = atol(argv[optind+1]);
        nb_threads = atoi(argv[optind+2]);
        assert(nb_towns > 0);
        assert(nb_threads > 0);

        minimum = INT_MAX;

        /* generer la carte et la matrice de distance */
        fprintf (stderr, "ncities = %3d\n", nb_towns);
        genmap ();

        init_queue (&q);

        clock_gettime (CLOCK_REALTIME, &t1);

        memset (path, -1, MAX_TOWNS * sizeof (int));
        path[0] = 0;

        /* mettre les travaux dans la file d'attente */
        generate_tsp_jobs (&q, 1, 0, path, &cuts, sol, & sol_len, 3);
        no_more_jobs (&q);

        /* calculer chacun des travaux */
        tsp_path_t solution;
        memset (solution, -1, MAX_TOWNS * sizeof (int));
        solution[0] = 0;
        int error;
        pthread_t threads[nb_threads];
        int threads_created = 0;
        pthread_attr_t attr;
        //Initialisation des mutex
        pthread_mutex_init(&m_sol, NULL);
        pthread_mutex_init(&m_cut, NULL);
        pthread_mutex_init(&m_min, NULL);
        pthread_mutex_init(&m_get_job, NULL);

        // Init des paramètres du thread : 
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        //Tableau de solution
        tsp_path_t tab_sol[nb_threads];

        int cpt = -1;

        //Tableau d'arguments
        struct tsp_args tab_args[nb_threads];


        // Execution des jobs
        while (!empty_queue (&q)) {
                int hops = 0, len = 0;

                // Ajout de threads ici :
                if(threads_created < nb_threads){
                        cpt++;

                        tab_args[cpt].hops = hops;
                        tab_args[cpt].len = len;

                        memset (tab_sol[cpt], -1, MAX_TOWNS * sizeof (int));
                        tab_sol[cpt][0] = 0;
                        tab_args[cpt].path = &tab_sol[cpt];
                        tab_args[cpt].cuts = &cuts;
                        tab_args[cpt].sol = &sol;
                        tab_args[cpt].sol_len = &sol_len;
                        tab_args[cpt].q = &q;
                        error = pthread_create(&threads[threads_created++], &attr, 
                                        tsp_thread, (void *)&tab_args[cpt]);
                        if (error){
                                fprintf(stderr, "Erreur lors de la création du thread.\n \
                                                pthread_create() a retourné le code : %d\n", error);
                                exit(EXIT_FAILURE);;
                        }
                } else {
                        get_job (&q, solution, &hops, &len);
                        tsp (hops, len, solution, &cuts, sol, &sol_len);
                }
        }

        /* Libère les parametres des threads, et attends pour leur terminaison */
        pthread_attr_destroy(&attr);
        int i;
        for(i=0; i<nb_threads; i++) {
                void *status;
                error = pthread_join(threads[i], &status);
                if (error) {
                        printf("Erreur lors de l'attente de terminaison d'un thread.\
                                        \npthread_join() a retourné le code %d\n", error);
                        exit(EXIT_FAILURE);
                }
        }

        clock_gettime (CLOCK_REALTIME, &t2);

        if (affiche_sol)
                print_solution_svg (sol, sol_len);

        perf = TIME_DIFF (t1,t2);
        printf("<!-- # = %d seed = %ld len = %d threads = %d time = %lld.%03lld ms ( %lld coupures ) -->\n",
                        nb_towns, myseed, sol_len, nb_threads,
                        perf/1000000ll, perf%1000000ll, cuts);

        pthread_mutex_destroy(&m_sol);
        pthread_mutex_destroy(&m_cut);
        pthread_mutex_destroy(&m_min);
        pthread_mutex_destroy(&m_get_job);
        pthread_exit(NULL);
        return 0 ;
}
