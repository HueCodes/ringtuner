#include <errno.h>
#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_ROWS 512u
#define MAX_SCENARIOS 64u

typedef struct {
    char scenario[96];
    char policy[96];
    unsigned packet_threshold;
    unsigned timer_threshold;
    double reward;
    double p99;
    double interrupts;
    double delivered_ratio;
    double drop_ratio;
    double delta;
} comparison_row_t;

typedef struct {
    char scenario[96];
    comparison_row_t best;
    comparison_row_t fixed_balanced;
    comparison_row_t tuned_direct;
    bool have_best;
    bool have_fixed;
    bool have_tuned;
} scenario_summary_t;

typedef struct {
    size_t rows;
    char best_label[128];
    double best_reward;
} artifact_summary_t;

static void usage(const char *prog) {
    printf("usage: %s [--comparison results/comparison.csv] [--pareto results/pareto.csv]\n"
           "          [--trace results/trace-microburst.csv] [--out results/report.md] [--help]\n",
           prog);
}

static bool results_path_allowed(const char *path) {
    return path != NULL && strncmp(path, "results/", 8u) == 0 && strstr(path, "..") == NULL;
}

static char *next_field(char **cursor) {
    char *field = *cursor;
    if (field == NULL) {
        return NULL;
    }
    char *comma = strchr(field, ',');
    if (comma != NULL) {
        *comma = '\0';
        *cursor = comma + 1;
    } else {
        *cursor = NULL;
    }
    field[strcspn(field, "\r\n")] = '\0';
    return field;
}

static bool read_comparison(const char *path, comparison_row_t rows[MAX_ROWS], size_t *row_count) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    char line[1024];
    *row_count = 0u;
    bool first = true;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (first) {
            first = false;
            continue;
        }
        if (*row_count >= MAX_ROWS) {
            fclose(f);
            return false;
        }
        char *cursor = line;
        char *field = NULL;
        comparison_row_t row;
        memset(&row, 0, sizeof(row));

        field = next_field(&cursor);
        if (field == NULL) {
            continue;
        }
        snprintf(row.scenario, sizeof(row.scenario), "%s", field);
        field = next_field(&cursor);
        if (field == NULL) {
            fclose(f);
            return false;
        }
        snprintf(row.policy, sizeof(row.policy), "%s", field);
        field = next_field(&cursor);
        row.packet_threshold = field == NULL ? 0u : (unsigned)strtoul(field, NULL, 10);
        field = next_field(&cursor);
        row.timer_threshold = field == NULL ? 0u : (unsigned)strtoul(field, NULL, 10);
        field = next_field(&cursor);
        row.reward = field == NULL ? 0.0 : strtod(field, NULL);
        field = next_field(&cursor);
        row.p99 = field == NULL ? 0.0 : strtod(field, NULL);
        field = next_field(&cursor);
        row.interrupts = field == NULL ? 0.0 : strtod(field, NULL);
        field = next_field(&cursor);
        row.delivered_ratio = field == NULL ? 0.0 : strtod(field, NULL);
        field = next_field(&cursor);
        row.drop_ratio = field == NULL ? 0.0 : strtod(field, NULL);
        field = next_field(&cursor);
        row.delta = field == NULL ? 0.0 : strtod(field, NULL);
        rows[(*row_count)++] = row;
    }
    fclose(f);
    return true;
}

static scenario_summary_t *summary_for(scenario_summary_t summaries[MAX_SCENARIOS],
                                       size_t *summary_count,
                                       const char *scenario) {
    for (size_t i = 0u; i < *summary_count; i++) {
        if (strcmp(summaries[i].scenario, scenario) == 0) {
            return &summaries[i];
        }
    }
    if (*summary_count >= MAX_SCENARIOS) {
        return NULL;
    }
    scenario_summary_t *s = &summaries[*summary_count];
    memset(s, 0, sizeof(*s));
    snprintf(s->scenario, sizeof(s->scenario), "%s", scenario);
    (*summary_count)++;
    return s;
}

static bool summarize_comparison(const comparison_row_t rows[MAX_ROWS],
                                 size_t row_count,
                                 scenario_summary_t summaries[MAX_SCENARIOS],
                                 size_t *summary_count) {
    *summary_count = 0u;
    for (size_t i = 0u; i < row_count; i++) {
        scenario_summary_t *s = summary_for(summaries, summary_count, rows[i].scenario);
        if (s == NULL) {
            return false;
        }
        if (!s->have_best || rows[i].reward > s->best.reward) {
            s->best = rows[i];
            s->have_best = true;
        }
        if (strcmp(rows[i].policy, "fixed_balanced") == 0) {
            s->fixed_balanced = rows[i];
            s->have_fixed = true;
        } else if (strcmp(rows[i].policy, "tuned_direct") == 0) {
            s->tuned_direct = rows[i];
            s->have_tuned = true;
        }
    }
    return true;
}

static bool summarize_generic_csv(const char *path, size_t reward_col, size_t label_col, artifact_summary_t *out) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->best_reward = -DBL_MAX;
    char line[2048];
    bool first = true;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (first) {
            first = false;
            continue;
        }
        char copy[2048];
        snprintf(copy, sizeof(copy), "%s", line);
        char *cursor = copy;
        char *field = NULL;
        size_t col = 0u;
        char label[128] = "";
        double reward = 0.0;
        bool have_reward = false;
        while ((field = next_field(&cursor)) != NULL) {
            if (col == label_col) {
                snprintf(label, sizeof(label), "%s", field);
            }
            if (col == reward_col) {
                reward = strtod(field, NULL);
                have_reward = true;
            }
            col++;
        }
        out->rows++;
        if (have_reward && reward > out->best_reward) {
            out->best_reward = reward;
            snprintf(out->best_label, sizeof(out->best_label), "%s", label);
        }
    }
    fclose(f);
    return true;
}

static void write_comparison_section(FILE *out,
                                     const scenario_summary_t summaries[MAX_SCENARIOS],
                                     size_t summary_count) {
    fprintf(out, "## Scenario Comparison\n\n");
    fprintf(out, "| scenario | best policy | reward | p99 | interrupts | drop | tuned delta |\n");
    fprintf(out, "| --- | --- | ---: | ---: | ---: | ---: | ---: |\n");
    for (size_t i = 0u; i < summary_count; i++) {
        const scenario_summary_t *s = &summaries[i];
        const double tuned_delta = s->have_tuned ? s->tuned_direct.delta : 0.0;
        fprintf(out,
                "| %s | %s | %.6f | %.3f | %.3f | %.6f | %.6f |\n",
                s->scenario,
                s->best.policy,
                s->best.reward,
                s->best.p99,
                s->best.interrupts,
                s->best.drop_ratio,
                tuned_delta);
    }
    fprintf(out, "\n");
}

static void write_artifacts(FILE *out,
                            const char *comparison_path,
                            const char *pareto_path,
                            const char *trace_path,
                            const artifact_summary_t *pareto,
                            const artifact_summary_t *trace) {
    fprintf(out, "## Artifacts\n\n");
    fprintf(out, "- `%s`: tuned policy comparison by built-in scenario.\n", comparison_path);
    if (pareto != NULL) {
        fprintf(out,
                "- `%s`: %zu Pareto frontier rows, best row traffic `%s` at reward %.6f.\n",
                pareto_path,
                pareto->rows,
                pareto->best_label,
                pareto->best_reward);
    }
    if (trace != NULL) {
        fprintf(out,
                "- `%s`: %zu trace replay rows, best policy `%s` at reward %.6f.\n",
                trace_path,
                trace->rows,
                trace->best_label,
                trace->best_reward);
    }
    fprintf(out, "- `results/tuning-scenario-traffic.csv`: train and eval rows for scenario-scoped tuning.\n");
    fprintf(out, "- `results/baselines.csv` and `results/scenarios.csv`: raw simulator metrics.\n\n");
}

int main(int argc, char **argv) {
    const char *comparison_path = "results/comparison.csv";
    const char *pareto_path = "results/pareto.csv";
    const char *trace_path = "results/trace-microburst.csv";
    const char *out_path = "results/report.md";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--comparison") == 0 && i + 1 < argc) {
            comparison_path = argv[++i];
        } else if (strcmp(argv[i], "--pareto") == 0 && i + 1 < argc) {
            pareto_path = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!results_path_allowed(comparison_path) || !results_path_allowed(pareto_path) ||
        !results_path_allowed(trace_path) || !results_path_allowed(out_path)) {
        fprintf(stderr, "paths must be under results/ and must not contain ..\n");
        return 2;
    }

    comparison_row_t rows[MAX_ROWS];
    size_t row_count = 0u;
    if (!read_comparison(comparison_path, rows, &row_count)) {
        fprintf(stderr, "failed to read %s\n", comparison_path);
        return 1;
    }

    scenario_summary_t summaries[MAX_SCENARIOS];
    size_t summary_count = 0u;
    if (!summarize_comparison(rows, row_count, summaries, &summary_count)) {
        fprintf(stderr, "failed to summarize comparison rows\n");
        return 1;
    }

    artifact_summary_t pareto;
    artifact_summary_t trace;
    const bool have_pareto = summarize_generic_csv(pareto_path, 7u, 2u, &pareto);
    const bool have_trace = summarize_generic_csv(trace_path, 30u, 2u, &trace);

    if (mkdir("results", 0777) != 0 && errno != EEXIST) {
        perror("mkdir results");
        return 1;
    }
    FILE *out = fopen(out_path, "w");
    if (out == NULL) {
        perror("fopen report");
        return 1;
    }

    fprintf(out, "# RingTuner Local Report\n\n");
    fprintf(out, "Generated by `make report` from local simulator runs.\n\n");
    write_comparison_section(out, summaries, summary_count);
    write_artifacts(out,
                    comparison_path,
                    pareto_path,
                    trace_path,
                    have_pareto ? &pareto : NULL,
                    have_trace ? &trace : NULL);
    fprintf(out, "## Read\n\n");
    fprintf(out, "The frontier file is the best place to inspect tradeoffs that a scalar reward hides. ");
    fprintf(out, "The comparison file is the fast answer for which built-in policy wins per scenario.\n");

    if (fclose(out) != 0) {
        perror("fclose report");
        return 1;
    }
    return 0;
}
