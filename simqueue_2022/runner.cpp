/* -*- C++ -*- */
/*******************************************************
            RUNNER . CPP

    Orchestratore degli esperimenti per il progetto C3.
    1. Lancia ./multiqueue --batch per tutte le
       configurazioni dei tre scenari (A/B/C).
    2. Scrive CSV con le metriche raccolte.
    3. Invoca gnuplot via popen() per produrre grafici PNG 
       .
*******************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <map>

// ---- Configurazione globale -------------------------------------
static const char* SIM_BIN  = "./queue";
static const char* GRAFDIR  = "grafici";

static const char* POL_NAME[3] = { "Random", "Round-robin", "JSQ" };
static const char* POL_COLOR[3] = { "#1f77b4", "#ff7f0e", "#2ca02c" };
static const char* POL_PT[3]   = { "pt 7 ps 1.4", "pt 5 ps 1.4", "pt 9 ps 1.6" };

// Risultato di una singola simulazione.
struct SimResult {
    int    policy;
    int    N;
    double lambda;
    double trans;
    double sim_T;
    double W;
    double imb;
    long   pkts;
    std::vector<double> Ls;
};

// -------------------------------------------------------------
// Sceglie durate (transitorio, misurazione) in funzione del carico.
// A carichi elevati la varianza esplode: serve piu' tempo simulato.
// -------------------------------------------------------------
static void choose_sim_params(double rho, double& trans, double& simlen) {
    if (rho >= 0.9)       { trans = 1500.0; simlen = 20000.0; }
    else if (rho >= 0.75) { trans = 1000.0; simlen = 12000.0; }
    else                  { trans = 500.0;  simlen = 6000.0;  }
}

// -------------------------------------------------------------
// Esegue una run del simulatore in modalita' batch e ne legge la
// riga CSV su stdout. Ritorna true in caso di successo.
// -------------------------------------------------------------
static bool run_one(int N, double lambda, const std::vector<double>& mus,
                    int policy, double trans, double simlen,
                    SimResult& out) {
    std::ostringstream cmd;
    cmd << SIM_BIN << " --batch " << N << " " << lambda;
    for (int i = 0; i < N; ++i) cmd << " " << mus[(size_t)i];
    cmd << " " << policy << " " << trans << " " << simlen;

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        std::fprintf(stderr, "popen failed: %s\n", cmd.str().c_str());
        return false;
    }
    char buf[1024];
    std::string csv_line;
    while (std::fgets(buf, sizeof(buf), pipe)) {
        if (std::strncmp(buf, "CSV,", 4) == 0) {
            csv_line = buf;
            break;
        }
    }
    pclose(pipe);
    if (csv_line.empty()) {
        std::fprintf(stderr, "Nessuna riga CSV per %s\n", cmd.str().c_str());
        return false;
    }
    // Parse: CSV,policy,N,lambda,trans,sim_T,W,imb,pkts,L0,L1,...
    std::vector<std::string> tok;
    {
        std::string s;
        for (char c : csv_line) {
            if (c == ',' || c == '\n') { tok.push_back(s); s.clear(); }
            else s.push_back(c);
        }
        if (!s.empty()) tok.push_back(s);
    }
    if (tok.size() < (size_t)(9 + N) || tok[0] != "CSV") {
        std::fprintf(stderr, "CSV malformata: %s\n", csv_line.c_str());
        return false;
    }
    out.policy = std::atoi(tok[1].c_str());
    out.N      = std::atoi(tok[2].c_str());
    out.lambda = std::atof(tok[3].c_str());
    out.trans  = std::atof(tok[4].c_str());
    out.sim_T  = std::atof(tok[5].c_str());
    out.W      = std::atof(tok[6].c_str());
    out.imb    = std::atof(tok[7].c_str());
    out.pkts   = std::atol(tok[8].c_str());
    out.Ls.clear();
    for (int i = 0; i < N; ++i) {
        out.Ls.push_back(std::atof(tok[(size_t)(9 + i)].c_str()));
    }
    return true;
}

// -------------------------------------------------------------
// Scrive un CSV completo (header + righe) su disco.
// -------------------------------------------------------------
static void write_csv(const std::string& path,
                      const std::string& header,
                      const std::vector<std::string>& rows) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) { std::perror(path.c_str()); std::exit(1); }
    std::fprintf(f, "%s\n", header.c_str());
    for (const auto& r : rows) std::fprintf(f, "%s\n", r.c_str());
    std::fclose(f);
}

static std::string sr_to_csv(const SimResult& r, double rho) {
    std::ostringstream s;
    s << POL_NAME[r.policy] << "," << r.N << "," << r.lambda << ","
      << rho << "," << r.W << "," << r.imb << "," << r.pkts << ","
      << r.sim_T;
    for (double L : r.Ls) s << "," << L;
    return s.str();
}

// -------------------------------------------------------------
// Helper: scrive le terne (x,y) per una serie in un file temporaneo
// che gnuplot poi caricheraì. Ritorna il path del file.
// -------------------------------------------------------------
static std::string write_xy(const std::string& path,
                            const std::vector<double>& xs,
                            const std::vector<double>& ys) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) { std::perror(path.c_str()); std::exit(1); }
    for (size_t i = 0; i < xs.size(); ++i) {
        std::fprintf(f, "%.6f %.6f\n", xs[i], ys[i]);
    }
    std::fclose(f);
    return path;
}

// -------------------------------------------------------------
// Invia uno script al binario gnuplot tramite popen.
// -------------------------------------------------------------
static void run_gnuplot(const std::string& script) {
    FILE* gp = popen("gnuplot", "w");
    if (!gp) { std::perror("gnuplot"); std::exit(1); }
    std::fputs(script.c_str(), gp);
    pclose(gp);
}

// Cornice comune per tutti i grafici (terminal pngcairo, stile, griglia).
static std::string gp_preamble(const std::string& outfile,
                               const std::string& title,
                               const std::string& xlabel,
                               const std::string& ylabel,
                               bool logy=false) {
    std::ostringstream s;
    s << "set terminal pngcairo size 900,560 enhanced font 'Helvetica,12'\n";
    s << "set output '" << outfile << "'\n";
    s << "set title \"" << title << "\"\n";
    s << "set xlabel \"" << xlabel << "\"\n";
    s << "set ylabel \"" << ylabel << "\"\n";
    s << "set grid linestyle 1 linecolor rgb '#cccccc'\n";
    s << "set key top left box opaque\n";
    s << "set border linewidth 1\n";
    if (logy) s << "set logscale y\n";
    return s.str();
}

// =============================================================
// Esperimenti.
// =============================================================
int main() {
    std::system((std::string("mkdir -p ") + GRAFDIR).c_str());

    // ---------- Scenario A: omogeneo ----------
    const int sA_N = 4;
    std::vector<double> sA_mu(sA_N, 1.0);
    std::vector<double> sA_lambdas = {1.0, 2.0, 2.5, 3.0, 3.2, 3.5, 3.8};
    std::map<int, std::vector<SimResult>> sA_data;

    std::printf("=== Scenario A (serventi omogenei) ===\n");
    std::vector<std::string> sA_rows;
    for (int p = 0; p < 3; ++p) {
        for (double lam : sA_lambdas) {
            double rho = lam / (double)sA_N; // mu_i=1
            double trans, simlen;
            choose_sim_params(rho, trans, simlen);
            SimResult r;
            if (!run_one(sA_N, lam, sA_mu, p, trans, simlen, r)) return 1;
            sA_data[p].push_back(r);
            sA_rows.push_back(sr_to_csv(r, rho));
            std::printf("  pol=%-12s lam=%.2f rho=%.2f W=%.4f imb=%.4f pkts=%ld\n",
                        POL_NAME[p], lam, rho, r.W, r.imb, r.pkts);
        }
    }
    write_csv("scenarioA.csv",
              "policy,N,lambda,rho,W,imbalance,pkts,sim_T,L0,L1,L2,L3", sA_rows);

    // ---------- Scenario B: eterogeneo ----------
    const int sB_N = 4;
    std::vector<double> sB_mu = {2.0, 1.5, 1.0, 0.5};
    double sB_mu_sum = 0.0; for (double m : sB_mu) sB_mu_sum += m; // = 5.0
    std::vector<double> sB_lambdas = {1.0, 2.0, 3.0, 4.0, 4.5};
    std::map<int, std::vector<SimResult>> sB_data;

    std::printf("\n=== Scenario B (serventi eterogenei) ===\n");
    std::vector<std::string> sB_rows;
    for (int p = 0; p < 3; ++p) {
        for (double lam : sB_lambdas) {
            double rho = lam / sB_mu_sum;
            double trans, simlen;
            choose_sim_params(rho, trans, simlen);
            SimResult r;
            if (!run_one(sB_N, lam, sB_mu, p, trans, simlen, r)) return 1;
            sB_data[p].push_back(r);
            sB_rows.push_back(sr_to_csv(r, rho));
            std::printf("  pol=%-12s lam=%.2f rho=%.2f W=%.4f imb=%.4f\n",
                        POL_NAME[p], lam, rho, r.W, r.imb);
        }
    }
    write_csv("scenarioB.csv",
              "policy,N,lambda,rho,W,imbalance,pkts,sim_T,L0,L1,L2,L3", sB_rows);

    // ---------- Scenario C: scalabilita' ----------
    std::vector<int> sC_Ns = {2, 4, 8, 16};
    std::map<int, std::vector<SimResult>> sC_data;
    std::printf("\n=== Scenario C (scalabilita') ===\n");
    std::vector<std::string> sC_rows;
    for (int p = 0; p < 3; ++p) {
        for (int N : sC_Ns) {
            std::vector<double> mus(N, 1.0);
            double lam = 0.8 * N;
            double trans, simlen;
            choose_sim_params(0.8, trans, simlen);
            SimResult r;
            if (!run_one(N, lam, mus, p, trans, simlen, r)) return 1;
            sC_data[p].push_back(r);
            std::ostringstream s;
            s << POL_NAME[p] << "," << N << "," << lam << ",0.8,"
              << r.W << "," << r.imb << "," << r.pkts << "," << r.sim_T;
            sC_rows.push_back(s.str());
            std::printf("  pol=%-12s N=%2d lam=%.2f W=%.4f imb=%.4f\n",
                        POL_NAME[p], N, lam, r.W, r.imb);
        }
    }
    write_csv("scenarioC.csv",
              "policy,N,lambda,rho_per_server,W,imbalance,pkts,sim_T", sC_rows);

    // =============================================================
    // Generazione grafici PNG con gnuplot.
    // =============================================================
    std::printf("\nGenero i grafici PNG...\n");

    auto make_line_plot = [&](const std::string& outfile,
                              const std::string& title,
                              const std::string& xlabel,
                              const std::string& ylabel,
                              std::map<int, std::vector<SimResult>>& data,
                              bool use_lambda_x, bool logy) {
        // Scrivi i datafile per ogni politica.
        std::vector<std::string> dfiles(3);
        for (int p = 0; p < 3; ++p) {
            std::vector<double> xs, ys;
            for (const SimResult& r : data[p]) {
                xs.push_back(use_lambda_x ? r.lambda : (double)r.N);
            }
            // y values from caller via lambda? simplify: write both W and imb files.
            (void)ys;
            dfiles[(size_t)p] = "";
        }
        (void)outfile; (void)title; (void)xlabel; (void)ylabel; (void)logy;
    };
    (void)make_line_plot; // non usata: scriviamo i grafici esplicitamente sotto.

    // --- Grafico 1: W vs lambda (Scenario A) ---
    {
        std::vector<std::string> df(3);
        for (int p = 0; p < 3; ++p) {
            std::vector<double> xs, ys;
            for (const SimResult& r : sA_data[p]) {
                xs.push_back(r.lambda);
                ys.push_back(r.W);
            }
            df[(size_t)p] = write_xy("grafici/_tmp_A_W_" +
                                     std::to_string(p) + ".dat", xs, ys);
        }
        std::ostringstream s;
        s << gp_preamble("grafici/fig1_W_vs_lambda_A.png",
                         "Scenario A: tempo medio di permanenza vs lambda "
                         "(N=4, mu_i=1)",
                         "lambda (pkt/s)", "W (s)");
        s << "plot \\\n";
        for (int p = 0; p < 3; ++p) {
            s << "  '" << df[(size_t)p] << "' using 1:2 with linespoints "
              << "linecolor rgb '" << POL_COLOR[p] << "' "
              << POL_PT[p] << " linewidth 2 "
              << "title '" << POL_NAME[p] << "'"
              << (p < 2 ? ", \\\n" : "\n");
        }
        run_gnuplot(s.str());
    }

    // --- Grafico 2: imbalance vs lambda (Scenario A) ---
    {
        std::vector<std::string> df(3);
        for (int p = 0; p < 3; ++p) {
            std::vector<double> xs, ys;
            for (const SimResult& r : sA_data[p]) {
                xs.push_back(r.lambda);
                ys.push_back(r.imb);
            }
            df[(size_t)p] = write_xy("grafici/_tmp_A_imb_" +
                                     std::to_string(p) + ".dat", xs, ys);
        }
        std::ostringstream s;
        s << gp_preamble("grafici/fig2_imb_vs_lambda_A.png",
                         "Scenario A: indice di sbilanciamento vs lambda "
                         "(N=4, mu_i=1)",
                         "lambda (pkt/s)",
                         "L_max - L_min (pkt)");
        s << "plot \\\n";
        for (int p = 0; p < 3; ++p) {
            s << "  '" << df[(size_t)p] << "' using 1:2 with linespoints "
              << "linecolor rgb '" << POL_COLOR[p] << "' "
              << POL_PT[p] << " linewidth 2 "
              << "title '" << POL_NAME[p] << "'"
              << (p < 2 ? ", \\\n" : "\n");
        }
        run_gnuplot(s.str());
    }

    // --- Grafico 3: W vs lambda (Scenario B) ---
    {
        std::vector<std::string> df(3);
        for (int p = 0; p < 3; ++p) {
            std::vector<double> xs, ys;
            for (const SimResult& r : sB_data[p]) {
                xs.push_back(r.lambda);
                ys.push_back(r.W);
            }
            df[(size_t)p] = write_xy("grafici/_tmp_B_W_" +
                                     std::to_string(p) + ".dat", xs, ys);
        }
        std::ostringstream s;
        s << gp_preamble("grafici/fig3_W_vs_lambda_B.png",
                         "Scenario B: tempo medio di permanenza vs lambda "
                         "(mu = [2.0, 1.5, 1.0, 0.5])",
                         "lambda (pkt/s)", "W (s) - scala log",
                         /*logy=*/true);
        s << "plot \\\n";
        for (int p = 0; p < 3; ++p) {
            s << "  '" << df[(size_t)p] << "' using 1:2 with linespoints "
              << "linecolor rgb '" << POL_COLOR[p] << "' "
              << POL_PT[p] << " linewidth 2 "
              << "title '" << POL_NAME[p] << "'"
              << (p < 2 ? ", \\\n" : "\n");
        }
        run_gnuplot(s.str());
    }

    // --- Grafico 4: imbalance vs lambda (Scenario B) ---
    {
        std::vector<std::string> df(3);
        for (int p = 0; p < 3; ++p) {
            std::vector<double> xs, ys;
            for (const SimResult& r : sB_data[p]) {
                xs.push_back(r.lambda);
                // Evita zero su scala log: aggiungi un offset minimo.
                ys.push_back(r.imb > 1e-6 ? r.imb : 1e-6);
            }
            df[(size_t)p] = write_xy("grafici/_tmp_B_imb_" +
                                     std::to_string(p) + ".dat", xs, ys);
        }
        std::ostringstream s;
        s << gp_preamble("grafici/fig4_imb_vs_lambda_B.png",
                         "Scenario B: indice di sbilanciamento vs lambda "
                         "(serventi eterogenei)",
                         "lambda (pkt/s)",
                         "L_max - L_min (pkt) - scala log",
                         /*logy=*/true);
        s << "plot \\\n";
        for (int p = 0; p < 3; ++p) {
            s << "  '" << df[(size_t)p] << "' using 1:2 with linespoints "
              << "linecolor rgb '" << POL_COLOR[p] << "' "
              << POL_PT[p] << " linewidth 2 "
              << "title '" << POL_NAME[p] << "'"
              << (p < 2 ? ", \\\n" : "\n");
        }
        run_gnuplot(s.str());
    }

    // --- Grafico 5: bar chart L_i per politica (Scenario B, lambda=2.0) ---
    // Scelta di lambda=2.0 (rho_globale=0.4): a questo carico il servente
    // piu' lento sotto Random/RR e' gia' al limite di stabilita' (lambda/N
    // = 0.5 = mu_3), il che evidenzia in modo netto lo squilibrio. A
    // lambda=4.0 (rho=0.8 come da traccia) la coda del servente lento
    // diverge in modo cosi' violento che JSQ diventa invisibile anche in
    // scala log; usiamo dunque scala log e lambda=2.0 per leggibilita'.
    {
        const double target = 2.0;
        std::string dat = "grafici/_tmp_B_bar.dat";
        FILE* f = std::fopen(dat.c_str(), "w");
        if (!f) { std::perror(dat.c_str()); return 1; }
        std::fprintf(f, "# server_index L_random L_RR L_JSQ mu_label\n");
        auto eps = [](double v) { return v > 0.01 ? v : 0.01; };
        for (int i = 0; i < sB_N; ++i) {
            double Lr=0, Lrr=0, Ljsq=0;
            for (const SimResult& r : sB_data[0]) if (r.lambda == target) Lr   = r.Ls[(size_t)i];
            for (const SimResult& r : sB_data[1]) if (r.lambda == target) Lrr  = r.Ls[(size_t)i];
            for (const SimResult& r : sB_data[2]) if (r.lambda == target) Ljsq = r.Ls[(size_t)i];
            std::fprintf(f, "%d %.6f %.6f %.6f mu=%g\n",
                         i, eps(Lr), eps(Lrr), eps(Ljsq), sB_mu[(size_t)i]);
        }
        std::fclose(f);
        std::ostringstream s;
        s << "set terminal pngcairo size 900,560 enhanced font 'Helvetica,12'\n";
        s << "set output 'grafici/fig5_bar_Ls_B.png'\n";
        s << "set title \"Scenario B: lunghezza media per coda a "
             "lambda=2.0 (mu = [2.0, 1.5, 1.0, 0.5]) - scala log Y\"\n";
        s << "set xlabel \"Servente i (con mu_i annotato)\"\n";
        s << "set ylabel \"L_i (pkt)\"\n";
        s << "set logscale y\n";
        s << "set yrange [0.05:200]\n";
        s << "set grid ytics linestyle 1 linecolor rgb '#cccccc'\n";
        s << "set key top left box opaque\n";
        s << "set style data histograms\n";
        s << "set style histogram clustered gap 1\n";
        s << "set style fill solid 0.85 border -1\n";
        s << "set boxwidth 0.9\n";
        s << "set xtics nomirror\n";
        s << "plot '" << dat << "' using 2:xtic(5) "
             "linecolor rgb '" << POL_COLOR[0] << "' title 'Random', \\\n"
             "     '' using 3        linecolor rgb '" << POL_COLOR[1] << "' title 'Round-robin', \\\n"
             "     '' using 4        linecolor rgb '" << POL_COLOR[2] << "' title 'JSQ'\n";
        run_gnuplot(s.str());
    }

    // --- Grafico 6: W vs N (Scenario C) ---
    {
        std::vector<std::string> df(3);
        for (int p = 0; p < 3; ++p) {
            std::vector<double> xs, ys;
            for (const SimResult& r : sC_data[p]) {
                xs.push_back((double)r.N);
                ys.push_back(r.W);
            }
            df[(size_t)p] = write_xy("grafici/_tmp_C_W_" +
                                     std::to_string(p) + ".dat", xs, ys);
        }
        std::ostringstream s;
        s << gp_preamble("grafici/fig6_W_vs_N_C.png",
                         "Scenario C: scalabilita' (W vs N, rho=0.8 per servente)",
                         "Numero di serventi N", "W (s)");
        s << "set xtics (2,4,8,16)\n";
        s << "plot \\\n";
        for (int p = 0; p < 3; ++p) {
            s << "  '" << df[(size_t)p] << "' using 1:2 with linespoints "
              << "linecolor rgb '" << POL_COLOR[p] << "' "
              << POL_PT[p] << " linewidth 2 "
              << "title '" << POL_NAME[p] << "'"
              << (p < 2 ? ", \\\n" : "\n");
        }
        run_gnuplot(s.str());
    }

    // Pulisce i file di dati temporanei usati da gnuplot.
    std::system((std::string("rm -f ") + GRAFDIR + "/_tmp_*.dat").c_str());

    std::printf("Fatto. Grafici in %s/, CSV: scenarioA.csv scenarioB.csv "
                "scenarioC.csv\n", GRAFDIR);
    return 0;
}
