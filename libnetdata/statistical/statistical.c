// SPDX-License-Identifier: GPL-3.0-or-later

#include "../libnetdata.h"

calculated_number default_single_exponential_smoothing_alpha = 0.1;

void log_series_to_stderr(calculated_number *series, size_t entries, calculated_number result, const char *msg) {
    const calculated_number *value, *end = &series[entries];

    fprintf(stderr, "%s of %zu entries [ ", msg, entries);
    for(value = series; value < end ;value++) {
        if(value != series) fprintf(stderr, ", ");
        fprintf(stderr, "%" CALCULATED_NUMBER_MODIFIER, *value);
    }
    fprintf(stderr, " ] results in " CALCULATED_NUMBER_FORMAT "\n", result);
}

// --------------------------------------------------------------------------------------------------------------------

inline calculated_number sum_and_count(const calculated_number *series, size_t entries, size_t *count) {
    const calculated_number *value, *end = &series[entries];
    calculated_number sum = 0;
    size_t c = 0;

    for(value = series; value < end ; value++) {
        if(calculated_number_isnumber(*value)) {
            sum += *value;
            c++;
        }
    }

    if(unlikely(!c)) sum = NAN;
    if(likely(count)) *count = c;

    return sum;
}

inline calculated_number sum(const calculated_number *series, size_t entries) {
    return sum_and_count(series, entries, NULL);
}

inline calculated_number average(const calculated_number *series, size_t entries) {
    size_t count = 0;
    calculated_number sum = sum_and_count(series, entries, &count);

    if(unlikely(!count)) return NAN;
    return sum / (calculated_number)count;
}

// --------------------------------------------------------------------------------------------------------------------

calculated_number moving_average(const calculated_number *series, size_t entries, size_t period) {
    if(unlikely(period <= 0))
        return 0.0;

    size_t i, count;
    calculated_number sum = 0, avg = 0;
    calculated_number p[period];

    for(count = 0; count < period ; count++)
        p[count] = 0.0;

    for(i = 0, count = 0; i < entries; i++) {
        calculated_number value = series[i];
        if(unlikely(!calculated_number_isnumber(value))) continue;

        if(unlikely(count < period)) {
            sum += value;
            avg = (count == period - 1) ? sum / (calculated_number)period : 0;
        }
        else {
            sum = sum - p[count % period] + value;
            avg = sum / (calculated_number)period;
        }

        p[count % period] = value;
        count++;
    }

    return avg;
}

// --------------------------------------------------------------------------------------------------------------------

static int qsort_compare(const void *a, const void *b) {
    calculated_number *p1 = (calculated_number *)a, *p2 = (calculated_number *)b;
    calculated_number n1 = *p1, n2 = *p2;

    if(unlikely(isnan(n1) || isnan(n2))) {
        if(isnan(n1) && !isnan(n2)) return -1;
        if(!isnan(n1) && isnan(n2)) return 1;
        return 0;
    }
    if(unlikely(isinf(n1) || isinf(n2))) {
        if(!isinf(n1) && isinf(n2)) return -1;
        if(isinf(n1) && !isinf(n2)) return 1;
        return 0;
    }

    if(unlikely(n1 < n2)) return -1;
    if(unlikely(n1 > n2)) return 1;
    return 0;
}

inline void sort_series(calculated_number *series, size_t entries) {
    qsort(series, entries, sizeof(calculated_number), qsort_compare);
}

inline calculated_number *copy_series(const calculated_number *series, size_t entries) {
    calculated_number *copy = mallocz(sizeof(calculated_number) * entries);
    memcpy(copy, series, sizeof(calculated_number) * entries);
    return copy;
}

calculated_number median_on_sorted_series(const calculated_number *series, size_t entries) {
    if(unlikely(entries == 0)) return NAN;
    if(unlikely(entries == 1)) return series[0];
    if(unlikely(entries == 2)) return (series[0] + series[1]) / 2;

    calculated_number average;
    if(entries % 2 == 0) {
        size_t m = entries / 2;
        average = (series[m] + series[m + 1]) / 2;
    }
    else {
        average = series[entries / 2];
    }

    return average;
}

calculated_number median(const calculated_number *series, size_t entries) {
    if(unlikely(entries == 0)) return NAN;
    if(unlikely(entries == 1)) return series[0];

    if(unlikely(entries == 2))
        return (series[0] + series[1]) / 2;

    calculated_number *copy = copy_series(series, entries);
    sort_series(copy, entries);

    calculated_number avg = median_on_sorted_series(copy, entries);

    freez(copy);
    return avg;
}

// --------------------------------------------------------------------------------------------------------------------

calculated_number moving_median(const calculated_number *series, size_t entries, size_t period) {
    if(entries <= period)
        return median(series, entries);

    calculated_number *data = copy_series(series, entries);

    size_t i;
    for(i = period; i < entries; i++) {
        data[i - period] = median(&series[i - period], period);
    }

    calculated_number avg = median(data, entries - period);
    freez(data);
    return avg;
}

// --------------------------------------------------------------------------------------------------------------------

// http://stackoverflow.com/a/15150143/4525767
calculated_number running_median_estimate(const calculated_number *series, size_t entries) {
    calculated_number median = 0.0f;
    calculated_number average = 0.0f;
    size_t i;

    for(i = 0; i < entries ; i++) {
        calculated_number value = series[i];
        if(unlikely(!calculated_number_isnumber(value))) continue;

        average += ( value - average ) * 0.1f; // rough running average.
        median += copysignl( average * 0.01, value - median );
    }

    return median;
}

// --------------------------------------------------------------------------------------------------------------------

calculated_number standard_deviation(const calculated_number *series, size_t entries) {
    if(unlikely(entries == 0)) return NAN;
    if(unlikely(entries == 1)) return series[0];

    const calculated_number *value, *end = &series[entries];
    size_t count;
    calculated_number sum;

    for(count = 0, sum = 0, value = series ; value < end ;value++) {
        if(likely(calculated_number_isnumber(*value))) {
            count++;
            sum += *value;
        }
    }

    if(unlikely(count == 0)) return NAN;
    if(unlikely(count == 1)) return sum;

    calculated_number average = sum / (calculated_number)count;

    for(count = 0, sum = 0, value = series ; value < end ;value++) {
        if(calculated_number_isnumber(*value)) {
            count++;
            sum += powl(*value - average, 2);
        }
    }

    if(unlikely(count == 0)) return NAN;
    if(unlikely(count == 1)) return average;

    calculated_number variance = sum / (calculated_number)(count); // remove -1 from count to have a population stddev
    calculated_number stddev = sqrtl(variance);
    return stddev;
}

// --------------------------------------------------------------------------------------------------------------------

calculated_number single_exponential_smoothing(const calculated_number *series, size_t entries, calculated_number alpha) {
    if(unlikely(entries == 0))
        return NAN;

    if(unlikely(isnan(alpha)))
        alpha = default_single_exponential_smoothing_alpha;

    const calculated_number *value = series, *end = &series[entries];
    calculated_number level = (1.0 - alpha) * (*value);

    for(value++ ; value < end; value++) {
        if(likely(calculated_number_isnumber(*value)))
            level = alpha * (*value) + (1.0 - alpha) * level;
    }

    return level;
}

calculated_number single_exponential_smoothing_reverse(const calculated_number *series, size_t entries, calculated_number alpha) {
    if(unlikely(entries == 0))
        return NAN;

    if(unlikely(isnan(alpha)))
        alpha = default_single_exponential_smoothing_alpha;

    const calculated_number *value = &series[entries -1];
    calculated_number level = (1.0 - alpha) * (*value);

    for(value++ ; value >= series; value--) {
        if(likely(calculated_number_isnumber(*value)))
            level = alpha * (*value) + (1.0 - alpha) * level;
    }

    return level;
}

// --------------------------------------------------------------------------------------------------------------------

// http://grisha.org/blog/2016/02/16/triple-exponential-smoothing-forecasting-part-ii/
calculated_number double_exponential_smoothing(const calculated_number *series, size_t entries, calculated_number alpha, calculated_number beta, calculated_number *forecast) {
    if(unlikely(entries == 0))
        return NAN;

    calculated_number level, trend;

    if(unlikely(isnan(alpha)))
        alpha = 0.3;

    if(unlikely(isnan(beta)))
        beta = 0.05;

    level = series[0];

    if(likely(entries > 1))
        trend = series[1] - series[0];
    else
        trend = 0;

    const calculated_number *value = series;
    for(value++ ; value >= series; value--) {
        if(likely(calculated_number_isnumber(*value))) {

            calculated_number last_level = level;
            level = alpha * *value + (1.0 - alpha) * (level + trend);
            trend = beta * (level - last_level) + (1.0 - beta) * trend;

        }
    }

    if(forecast)
        *forecast = level + trend;

    return level;
}

// --------------------------------------------------------------------------------------------------------------------

/*
 * Based on th R implementation
 *
 * a: level component
 * b: trend component
 * s: seasonal component
 *
 * Additive:
 *
 *   Yhat[t+h] = a[t] + h * b[t] + s[t + 1 + (h - 1) mod p],
 *   a[t] = α (Y[t] - s[t-p]) + (1-α) (a[t-1] + b[t-1])
 *   b[t] = β (a[t] - a[t-1]) + (1-β) b[t-1]
 *   s[t] = γ (Y[t] - a[t]) + (1-γ) s[t-p]
 *
 * Multiplicative:
 *
 *   Yhat[t+h] = (a[t] + h * b[t]) * s[t + 1 + (h - 1) mod p],
 *   a[t] = α (Y[t] / s[t-p]) + (1-α) (a[t-1] + b[t-1])
 *   b[t] = β (a[t] - a[t-1]) + (1-β) b[t-1]
 *   s[t] = γ (Y[t] / a[t]) + (1-γ) s[t-p]
 */
static int __HoltWinters(
        const calculated_number *series,
        int          entries,      // start_time + h

        calculated_number alpha,        // alpha parameter of Holt-Winters Filter.
        calculated_number beta,         // beta  parameter of Holt-Winters Filter. If set to 0, the function will do exponential smoothing.
        calculated_number gamma,        // gamma parameter used for the seasonal component. If set to 0, an non-seasonal model is fitted.

        const int *seasonal,
        const int *period,
        const calculated_number *a,      // Start value for level (a[0]).
        const calculated_number *b,      // Start value for trend (b[0]).
        calculated_number *s,            // Vector of start values for the seasonal component (s_1[0] ... s_p[0])

        /* return values */
        calculated_number *SSE,          // The final sum of squared errors achieved in optimizing
        calculated_number *level,        // Estimated values for the level component (size entries - t + 2)
        calculated_number *trend,        // Estimated values for the trend component (size entries - t + 2)
        calculated_number *season        // Estimated values for the seasonal component (size entries - t + 2)
)
{
    if(unlikely(entries < 4))
        return 0;

    int start_time = 2;

    calculated_number res = 0, xhat = 0, stmp = 0;
    int i, i0, s0;

    /* copy start values to the beginning of the vectors */
    level[0] = *a;
    if(beta > 0) trend[0] = *b;
    if(gamma > 0) memcpy(season, s, *period * sizeof(calculated_number));

    for(i = start_time - 1; i < entries; i++) {
        /* indices for period i */
        i0 = i - start_time + 2;
        s0 = i0 + *period - 1;

        /* forecast *for* period i */
        xhat = level[i0 - 1] + (beta > 0 ? trend[i0 - 1] : 0);
        stmp = gamma > 0 ? season[s0 - *period] : (*seasonal != 1);
        if (*seasonal == 1)
            xhat += stmp;
        else
            xhat *= stmp;

        /* Sum of Squared Errors */
        res   = series[i] - xhat;
        *SSE += res * res;

        /* estimate of level *in* period i */
        if (*seasonal == 1)
            level[i0] = alpha       * (series[i] - stmp)
                        + (1 - alpha) * (level[i0 - 1] + trend[i0 - 1]);
        else
            level[i0] = alpha       * (series[i] / stmp)
                        + (1 - alpha) * (level[i0 - 1] + trend[i0 - 1]);

        /* estimate of trend *in* period i */
        if (beta > 0)
            trend[i0] = beta        * (level[i0] - level[i0 - 1])
                        + (1 - beta)  * trend[i0 - 1];

        /* estimate of seasonal component *in* period i */
        if (gamma > 0) {
            if (*seasonal == 1)
                season[s0] = gamma       * (series[i] - level[i0])
                             + (1 - gamma) * stmp;
            else
                season[s0] = gamma       * (series[i] / level[i0])
                             + (1 - gamma) * stmp;
        }
    }

    return 1;
}

calculated_number holtwinters(const calculated_number *series, size_t entries, calculated_number alpha, calculated_number beta, calculated_number gamma, calculated_number *forecast) {
    if(unlikely(isnan(alpha)))
        alpha = 0.3;

    if(unlikely(isnan(beta)))
        beta = 0.05;

    if(unlikely(isnan(gamma)))
        gamma = 0;

    int seasonal = 0;
    int period = 0;
    calculated_number a0 = series[0];
    calculated_number b0 = 0;
    calculated_number s[] = {};

    calculated_number errors = 0.0;
    size_t nb_computations = entries;
    calculated_number *estimated_level  = callocz(nb_computations, sizeof(calculated_number));
    calculated_number *estimated_trend  = callocz(nb_computations, sizeof(calculated_number));
    calculated_number *estimated_season = callocz(nb_computations, sizeof(calculated_number));

    int ret = __HoltWinters(
            series,
            (int)entries,
            alpha,
            beta,
            gamma,
            &seasonal,
            &period,
            &a0,
            &b0,
            s,
            &errors,
            estimated_level,
            estimated_trend,
            estimated_season
    );

    calculated_number value = estimated_level[nb_computations - 1];

    if(forecast)
        *forecast = 0.0;

    freez(estimated_level);
    freez(estimated_trend);
    freez(estimated_season);

    if(!ret)
        return 0.0;

    return value;
}
