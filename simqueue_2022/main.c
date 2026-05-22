/* -*- C++ -*- */
/*******************************************************
            MAIN . C

    Entry point del simulatore multi-coda (progetto C3).

    Supporta due modalita':
      - interattiva (default): chiede i parametri da
        prompt e stampa i risultati in forma leggibile;
      - batch (--batch): riceve i parametri da riga di
        comando e stampa il risultato in formato CSV.
*******************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "global.h"
#include "queue.h"
#include "simulator.h"

static void batch_usage() {
    std::fprintf(stderr,
        "Batch usage:\n"
        "  queue --batch <N> <lambda> <mu_0> ... <mu_{N-1}> "
        "<policy> <trans> <simlen>\n"
        "  policy: 0=Random, 1=Round-robin, 2=JSQ\n");
    std::exit(1);
}

static int run_batch(int argc, char* argv[]) {
    // argv[1] == "--batch". Atteso: argc == 2 + 1 (N) + 1 (lam) + N + 3.
    if (argc < 3) batch_usage();
    int N = std::atoi(argv[2]);
    if (N < 1) batch_usage();
    int expected = 2 + 1 + 1 + N + 3;
    if (argc != expected) batch_usage();

    double lambda = std::atof(argv[3]);
    std::vector<double> mus;
    mus.reserve((size_t)N);
    for (int i = 0; i < N; ++i) {
        mus.push_back(std::atof(argv[4 + i]));
    }
    int policy    = std::atoi(argv[4 + N]);
    double trans  = std::atof(argv[5 + N]);
    double simlen = std::atof(argv[6 + N]);

    // Il costruttore base parsa -o/-t: gli passiamo un argv minimale.
    char* fake_argv[1] = { argv[0] };
    queue* sim = new queue(1, fake_argv);
    sim->set_batch(N, lambda, mus, policy, trans, simlen);
    sim->init();
    sim->run();
    sim->results_csv();
    delete sim;
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::strcmp(argv[1], "--batch") == 0) {
        return run_batch(argc, argv);
    }

    std::printf("\n*********************************************************\n");
    std::printf("    SIMULATORE MULTI-CODA N SERVENTI - progetto C3\n");
    std::printf("*********************************************************\n");

    simulator* eval = new queue(argc, argv);
    eval->init();
    eval->run();
    eval->results();
    delete eval;
    return 0;
}
