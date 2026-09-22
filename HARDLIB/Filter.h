#ifndef __FILTER_H__
#define __FILTER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-dimensional scalar Kalman filter state.
 *
 * This filter is intended for a single measured value, such as an ADC code.
 * The implementation uses the standard two-step scalar Kalman algorithm:
 * prediction followed by measurement correction.
 */
typedef struct
{
    float process_noise;       /*!< Process noise covariance (Q). */
    float measurement_noise;   /*!< Measurement noise covariance (R). */
    float estimate;            /*!< Current filtered estimate (x). */
    float estimate_error;      /*!< Estimate error covariance (P). */
    float kalman_gain;         /*!< Kalman gain calculated at last update (K). */
    uint8_t initialized;       /*!< Non-zero after successful initialization. */
} KalmanFilter_t;

/**
 * @brief Five-point moving-average filter state.
 */
typedef struct
{
    float samples[5];       /*!< Circular buffer containing recent samples. */
    float sum;              /*!< Sum of valid samples in the buffer. */
    uint8_t index;          /*!< Position for the next sample. */
    uint8_t count;          /*!< Number of valid samples, from 0 to 5. */
} MovingAverage5_t;

/**
 * @brief First-order low-pass filter state.
 *
 * The update equation is:
 * output = output + alpha * (input - output)
 */
typedef struct
{
    float alpha;            /*!< Smoothing factor, normally 0 < alpha <= 1. */
    float output;           /*!< Current filter output. */
    uint8_t initialized;     /*!< Non-zero after the first valid sample. */
} FirstOrderLowPass_t;

/** @brief Initialize a five-point moving-average filter. */
void MovingAverage5_Init(MovingAverage5_t *filter);

/** @brief Add one sample and return the current moving average. */
float MovingAverage5_Update(MovingAverage5_t *filter, float sample);

/** @brief Initialize a first-order low-pass filter. */
void FirstOrderLowPass_Init(FirstOrderLowPass_t *filter, float alpha);

/** @brief Add one sample and return the current low-pass output. */
float FirstOrderLowPass_Update(FirstOrderLowPass_t *filter, float sample);

/**
 * @brief Initialize a scalar Kalman filter.
 *
 * @param filter            Pointer to the filter state.
 * @param process_noise     Process noise covariance Q. Must be greater than 0.
 * @param measurement_noise Measurement noise covariance R. Must be greater than 0.
 * @param initial_estimate  Initial value of the filtered estimate.
 * @param initial_error     Initial estimate error covariance P. Must be greater than 0.
 */
void KalmanFilter_Init(KalmanFilter_t *filter,
                       float process_noise,
                       float measurement_noise,
                       float initial_estimate,
                       float initial_error);

/**
 * @brief Update the filter with one new measurement.
 *
 * @param filter       Pointer to an initialized filter state.
 * @param measurement  New measured value.
 * @return The updated filtered value. If filter is NULL, measurement is returned.
 */
float KalmanFilter_Update(KalmanFilter_t *filter, float measurement);

/**
 * @brief Reset the filter estimate while keeping its Q and R parameters.
 *
 * @param filter            Pointer to the filter state.
 * @param initial_estimate  Value used as the new initial estimate.
 */
void KalmanFilter_Reset(KalmanFilter_t *filter, float initial_estimate);

#ifdef __cplusplus
}
#endif

#endif /* __FILTER_H__ */
