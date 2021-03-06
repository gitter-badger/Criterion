/*
 * The MIT License (MIT)
 *
 * Copyright © 2015 Franklin "Snaipe" Mathieu <http://snai.pe/>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#define _GNU_SOURCE
#define CRITERION_LOGGING_COLORS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "criterion/stats.h"
#include "criterion/logging.h"
#include "criterion/options.h"
#include "criterion/ordered-set.h"
#include "timer.h"
#include "config.h"
#include "i18n.h"
#include "posix-compat.h"
#include "common.h"

#ifdef VANILLA_WIN32
// fallback to strtok on windows since strtok_s is not available everywhere
# define strtok_r(str, delim, saveptr) strtok(str, delim)
#endif

#ifdef _MSC_VER
# define strdup _strdup
#endif

typedef const char *const msg_t;

// Used to mark string for gettext
# define N_(Str) Str
# define N_s(Str, Pl) {Str, Pl}

static msg_t msg_pre_all = N_("Criterion v%s\n");
static msg_t msg_desc = N_("  %s\n");

#ifdef ENABLE_NLS
static msg_t msg_pre_init = N_("%1$s::%2$s\n");
static msg_t msg_post_test_timed = N_("%1$s::%2$s: (%3$3.2fs)\n");
static msg_t msg_post_test = N_("%1$s::%2$s\n");
static msg_t msg_post_suite_test = N_("%1$s::%2$s: Test is disabled\n");
static msg_t msg_post_suite_suite = N_("%1$s::%2$s: Suite is disabled\n");
static msg_t msg_assert_fail = N_("%1$s%2$s%3$s:%4$s%5$d%6$s: Assertion failed: %7$s\n");
static msg_t msg_theory_fail = N_("  Theory %1$s::%2$s failed with the following parameters: (%3$s)\n");
static msg_t msg_test_crash_line = N_("%1$s%2$s%3$s:%4$s%5$u%6$s: Unexpected signal caught below this line!\n");
static msg_t msg_test_crash = N_("%1$s::%2$s: CRASH!\n");
static msg_t msg_test_other_crash = N_("%1$sWarning! The test `%2$s::%3$s` crashed during its setup or teardown.%4$s\n");
static msg_t msg_test_abnormal_exit = N_("%1$sWarning! The test `%2$s::%3$s` exited during its setup or teardown.%4$s\n");
static msg_t msg_pre_suite[] = N_s("Running %1$s%2$lu%3$s test from %4$s%5$s%6$s:\n",
             "Running %1$s%2$lu%3$s tests from %4$s%5$s%6$s:\n");
static msg_t msg_post_all = N_("%1$sSynthesis: Tested: %2$s%3$lu%4$s "
             "| Passing: %5$s%6$lu%7$s "
             "| Failing: %8$s%9$lu%10$s "
             "| Crashing: %11$s%12$lu%13$s "
             "%14$s\n");
#else
static msg_t msg_pre_init = "%s::%s\n";
static msg_t msg_post_test_timed = "%s::%s: (%3.2fs)\n";
static msg_t msg_post_test = "%s::%s\n";
static msg_t msg_post_suite_test = "%s::%s: Test is disabled\n";
static msg_t msg_post_suite_suite = "%s::%s: Suite is disabled\n";
static msg_t msg_assert_fail = "%s%s%s:%s%d%s: Assertion failed: %s\n";
static msg_t msg_theory_fail = "  Theory %s::%s failed with the following parameters: %s\n";
static msg_t msg_test_crash_line = "%s%s%s:%s%u%s: Unexpected signal caught below this line!\n";
static msg_t msg_test_crash = "%s::%s: CRASH!\n";
static msg_t msg_test_other_crash = "%sWarning! The test `%s::%s` crashed during its setup or teardown.%s\n";
static msg_t msg_test_abnormal_exit = "%sWarning! The test `%s::%s` exited during its setup or teardown.%s\n";
static msg_t msg_pre_suite[] = { "Running %s%lu%s test from %s%s%s:\n",
            "Running %s%lu%s tests from %s%s%s:\n" };
static msg_t msg_post_all = "%sSynthesis: Tested: %s%lu%s "
            "| Passing: %s%lu%s "
            "| Failing: %s%lu%s "
            "| Crashing: %s%lu%s "
            "%s\n";
#endif

void normal_log_pre_all(UNUSED struct criterion_test_set *set) {
    criterion_pinfo(CRITERION_PREFIX_DASHES, _(msg_pre_all), VERSION);
}

void normal_log_pre_init(struct criterion_test *test) {
    criterion_pinfo(CRITERION_PREFIX_RUN, _(msg_pre_init),
            test->category,
            test->name);

    if (test->data->description)
        criterion_pinfo(CRITERION_PREFIX_RUN, _(msg_desc),
                test->data->description);
}

void normal_log_post_test(struct criterion_test_stats *stats) {
    const char *format = can_measure_time() ? msg_post_test_timed : msg_post_test;

    const enum criterion_logging_level level
            = stats->failed ? CRITERION_IMPORTANT : CRITERION_INFO;
    const struct criterion_prefix_data *prefix
            = stats->failed ? CRITERION_PREFIX_FAIL : CRITERION_PREFIX_PASS;

    criterion_plog(level, prefix, _(format),
            stats->test->category,
            stats->test->name,
            stats->elapsed_time);
}

static INLINE bool is_disabled(struct criterion_test *t,
                               struct criterion_suite *s) {
    return t->data->disabled || (s->data && s->data->disabled);
}

void normal_log_post_suite(struct criterion_suite_stats *stats) {
    for (struct criterion_test_stats *ts = stats->tests; ts; ts = ts->next) {
        if (is_disabled(ts->test, stats->suite)) {
            const char *format = ts->test->data->disabled
                    ? _(msg_post_suite_test)
                    : _(msg_post_suite_suite);

            criterion_pinfo(CRITERION_PREFIX_SKIP, format,
                    ts->test->category,
                    ts->test->name);

            if (ts->test->data->description)
                criterion_pinfo(CRITERION_PREFIX_DASHES, msg_desc,
                        ts->test->data->description);
        }
    }
}

void normal_log_post_all(struct criterion_global_stats *stats) {
    size_t tested = stats->nb_tests - stats->tests_skipped;

    criterion_pimportant(CRITERION_PREFIX_EQUALS,
            _(msg_post_all),
            FG_BOLD,
            FG_BLUE,  (unsigned long) tested,               FG_BOLD,
            FG_GREEN, (unsigned long) stats->tests_passed,  FG_BOLD,
            FG_RED,   (unsigned long) stats->tests_failed,  FG_BOLD,
            FG_RED,   (unsigned long) stats->tests_crashed, FG_BOLD,
            RESET);
}

void normal_log_assert(struct criterion_assert_stats *stats) {
    if (!stats->passed) {
        char *dup       = strdup(*stats->message ? stats->message : "");

#ifdef VANILLA_WIN32
        char *line      = strtok(dup, "\n");
#else
        char *saveptr   = NULL;
        char *line      = strtok_r(dup, "\n", &saveptr);
#endif

        bool sf = criterion_options.short_filename;
        criterion_pimportant(CRITERION_PREFIX_DASHES,
                _(msg_assert_fail),
                FG_BOLD, sf ? basename_compat(stats->file) : stats->file, RESET,
                FG_RED,  stats->line, RESET,
                line);

#ifdef VANILLA_WIN32
        while ((line = strtok(NULL, "\n")))
#else
        while ((line = strtok_r(NULL, "\n", &saveptr)))
#endif
            criterion_pimportant(CRITERION_PREFIX_DASHES, _(msg_desc), line);
        free(dup);
    }
}

void normal_log_test_crash(struct criterion_test_stats *stats) {
    bool sf = criterion_options.short_filename;
    criterion_pimportant(CRITERION_PREFIX_DASHES,
            _(msg_test_crash_line),
            FG_BOLD, sf ? basename_compat(stats->file) : stats->file, RESET,
            FG_RED,  stats->progress, RESET);
    criterion_pimportant(CRITERION_PREFIX_FAIL, _(msg_test_crash),
            stats->test->category,
            stats->test->name);
}

void normal_log_other_crash(UNUSED struct criterion_test_stats *stats) {
    criterion_pimportant(CRITERION_PREFIX_DASHES,
            _(msg_test_other_crash),
            FG_BOLD, stats->test->category, stats->test->name, RESET);
}

void normal_log_abnormal_exit(UNUSED struct criterion_test_stats *stats) {
    criterion_pimportant(CRITERION_PREFIX_DASHES,
            _(msg_test_abnormal_exit),
            FG_BOLD, stats->test->category, stats->test->name, RESET);
}

void normal_log_pre_suite(struct criterion_suite_set *set) {
    criterion_pinfo(CRITERION_PREFIX_EQUALS,
            _s(msg_pre_suite[0], msg_pre_suite[1], set->tests->size),
            FG_BLUE, (unsigned long) set->tests->size, RESET,
            FG_GOLD, set->suite.name,  RESET);
}

void normal_log_theory_fail(struct criterion_theory_stats *stats) {
    criterion_pimportant(CRITERION_PREFIX_DASHES,
            _(msg_theory_fail), 
            stats->stats->test->category,
            stats->stats->test->name,
            stats->formatted_args);
}

struct criterion_output_provider normal_logging = {
    .log_pre_all        = normal_log_pre_all,
    .log_pre_init       = normal_log_pre_init,
    .log_pre_suite      = normal_log_pre_suite,
    .log_assert         = normal_log_assert,
    .log_theory_fail    = normal_log_theory_fail,
    .log_test_crash     = normal_log_test_crash,
    .log_other_crash    = normal_log_other_crash,
    .log_abnormal_exit  = normal_log_abnormal_exit,
    .log_post_test      = normal_log_post_test,
    .log_post_suite     = normal_log_post_suite,
    .log_post_all       = normal_log_post_all,
};
