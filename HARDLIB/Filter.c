#include "Filter.h"

void MovingAverage5_Init(MovingAverage5_t *filter)
{
    uint8_t i;

    if (filter == 0)
        return;

    for (i = 0U; i < 5U; i++)
        filter->samples[i] = 0.0f;

    filter->sum = 0.0f;
    filter->index = 0U;
    filter->count = 0U;
}

float MovingAverage5_Update(MovingAverage5_t *filter, float sample)
{
    if (filter == 0)
        return sample;

    if (filter->count < 5U)
    {
        filter->samples[filter->index] = sample;
        filter->sum += sample;
        filter->count++;
    }
    else
    {
        filter->sum -= filter->samples[filter->index];
        filter->samples[filter->index] = sample;
        filter->sum += sample;
    }

    filter->index++;
    if (filter->index >= 5U)
        filter->index = 0U;

    return filter->sum / (float)filter->count;
}

void FirstOrderLowPass_Init(FirstOrderLowPass_t *filter, float alpha)
{
    if (filter == 0)
        return;

    if (alpha <= 0.0f)
        alpha = 0.01f;
    else if (alpha > 1.0f)
        alpha = 1.0f;

    filter->alpha = alpha;
    filter->output = 0.0f;
    filter->initialized = 0U;
}

float FirstOrderLowPass_Update(FirstOrderLowPass_t *filter, float sample)
{
    if (filter == 0)
        return sample;

    if (!filter->initialized)
    {
        filter->output = sample;
        filter->initialized = 1U;
    }
    else
    {
        filter->output += filter->alpha * (sample - filter->output);
    }

    return filter->output;
}

/**
 * @brief Initialize a scalar Kalman filter.
 *
 * Non-positive covariance values are replaced with a small positive default
 * so that the filter remains numerically valid on embedded targets.
 */
void KalmanFilter_Init(KalmanFilter_t *filter,
                       float process_noise,
                       float measurement_noise,
                       float initial_estimate,
                       float initial_error)
{
    if (filter == 0)
        return;

    filter->process_noise = (process_noise > 0.0f) ? process_noise : 0.0001f;
    filter->measurement_noise = (measurement_noise > 0.0f) ?
                                measurement_noise : 1.0f;
    filter->estimate = initial_estimate;
    filter->estimate_error = (initial_error > 0.0f) ? initial_error : 1.0f;
    filter->kalman_gain = 0.0f;
    filter->initialized = 1U;
}

/**
 * @brief Apply one scalar Kalman prediction/correction cycle.
 */
float KalmanFilter_Update(KalmanFilter_t *filter, float measurement)
{
    float predicted_error;

    if (filter == 0)
        return measurement;

    if (!filter->initialized)
    {
        filter->estimate = measurement;
        filter->estimate_error = 1.0f;
        filter->kalman_gain = 0.0f;
        filter->initialized = 1U;
    }

    /* Prediction: increase uncertainty by the process noise. */
    predicted_error = filter->estimate_error + filter->process_noise;

    /* Correction: calculate the gain and update the estimate. */
    filter->kalman_gain = predicted_error /
                          (predicted_error + filter->measurement_noise);
    filter->estimate += filter->kalman_gain *
                        (measurement - filter->estimate);

    /* Update the remaining estimate uncertainty. */
    filter->estimate_error = (1.0f - filter->kalman_gain) * predicted_error;

    return filter->estimate;
}

/**
 * @brief Reset the filter to a new estimate without changing Q or R.
 */
void KalmanFilter_Reset(KalmanFilter_t *filter, float initial_estimate)
{
    if (filter == 0)
        return;

    filter->estimate = initial_estimate;
    filter->estimate_error = 1.0f;
    filter->kalman_gain = 0.0f;
    filter->initialized = 1U;
}
