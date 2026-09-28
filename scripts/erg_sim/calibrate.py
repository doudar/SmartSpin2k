"""Identify an effective bike transfer function, with chronological holdout."""

from dataclasses import asdict
import numpy as np

from .model import BikeConfig


def metrics(actual, predicted):
    valid = np.isfinite(actual) & np.isfinite(predicted)
    a, p = np.asarray(actual)[valid], np.asarray(predicted)[valid]
    if not len(a):
        return {"samples": 0}
    e = p-a
    return {"samples": len(a), "mae_w": float(np.mean(np.abs(e))), "rmse_w": float(np.sqrt(np.mean(e*e))),
            "bias_w": float(np.mean(e)), "p95_absolute_error_w": float(np.percentile(np.abs(e), 95)),
            "within_20w_fraction": float(np.mean(np.abs(e) <= 20))}


def predict_reference(data, config, dt=.1):
    from scipy.signal import lfilter
    grid = np.arange(0, data[-1, 0]+dt, dt)
    position = np.interp(grid, data[:, 0], data[:, 3])
    cadence = np.interp(grid, data[:, 0], data[:, 2])
    static = np.interp(position, config.position_knots, config.watts_at_80rpm) * (cadence/80)**config.cadence_exponent
    rho = np.exp(-dt/config.lag_s) if config.lag_s > 0 else 0
    filtered, _ = lfilter([1-rho], [1, -rho], static, zi=[rho*data[0, 1]])
    return np.interp(data[:, 0]-config.delay_s, grid, filtered, left=data[0, 1])


def fit_bike(data, train_fraction=.65):
    from scipy.optimize import least_squares

    if not .3 <= train_fraction <= .8:
        raise ValueError("Training fraction must leave a substantial holdout (0.3 to 0.8)")
    split = data[-1, 0]*train_fraction
    # Remove stops and observations directly following missing log intervals.
    last_gap = -100.0
    eligible = np.ones(len(data), dtype=bool)
    for i in range(1, len(data)):
        if data[i, 0]-data[i-1, 0] > 3:
            last_gap = data[i, 0]
        eligible[i] = data[i, 0]-last_gap > 10
    eligible &= (data[:, 2] >= 40) & (data[:, 1] > 0)
    training = eligible & (data[:, 0] < split)
    validation = eligible & ~training
    if training.sum() < 100 or validation.sum() < 100:
        raise ValueError("Need at least 100 pedaling samples in training and holdout")
    knots = [0, 6000, 8000, 10000, 12000, 14000, 16000, 18000, 20000, 24482]
    initial = np.array([8, 25, 50, 80, 100, 130, 160, 200, 400, 1.5, 1.5, 1.5])

    def config(x):
        return BikeConfig(position_knots=knots, watts_at_80rpm=[0, *np.cumsum(x[:9]).tolist()],
                          cadence_exponent=float(x[9]), delay_s=float(x[10]), lag_s=float(x[11]), noise_w=0)

    def residual(x):
        return (predict_reference(data, config(x))-data[:, 1])[training]

    result = least_squares(residual, initial, bounds=([0]*9+[.5, 0, .05], [2000]*9+[3, 8, 8]),
                           loss="soft_l1", f_scale=12, max_nfev=160, diff_step=.001)
    fitted = config(result.x)
    prediction = predict_reference(data, fitted)
    # Effective stationary variation includes model mismatch and rider/sensor
    # noise; do not label it as a calibrated meter accuracy specification.
    stationary = training & np.r_[False, np.abs(np.diff(data[:, 3])) < 30] & np.r_[False, np.abs(np.diff(data[:, 2])) <= 1]
    errors = data[:, 1]-prediction
    stable_errors = errors[stationary]
    fitted.noise_w = float(1.4826*np.median(np.abs(stable_errors-np.median(stable_errors))))
    pairs = stationary[:-1] & stationary[1:]
    rho = float(np.corrcoef(errors[:-1][pairs], errors[1:][pairs])[0, 1]) if pairs.sum() >= 20 else 0
    fitted.noise_correlation_s = float(-np.median(np.diff(data[:, 0]))/np.log(np.clip(rho, .01, .98))) if rho > 0 else 0
    # Zero-lag model is independently refitted, not just the fitted model with lag removed.
    def static_residual(x):
        cfg = config(np.r_[x, 0, .05]); cfg.lag_s = 0
        return (predict_reference(data, cfg)-data[:, 1])[training]
    baseline = least_squares(static_residual, result.x[:10], bounds=([0]*9+[.5], [2000]*9+[3]),
                             loss="soft_l1", f_scale=12, max_nfev=100)
    baseline_cfg = config(np.r_[baseline.x, 0, .05]); baseline_cfg.lag_s = 0
    no_lag = predict_reference(data, baseline_cfg)
    changes = np.r_[False, np.abs(np.diff(data[:, 4])) >= 100]
    last_move, transient = -100, np.zeros(len(data), dtype=bool)
    for i in range(len(data)):
        if changes[i]: last_move = data[i, 0]
        transient[i] = data[i, 0]-last_move < 10
    report = {"training_end_s": float(split), "training_fraction": train_fraction,
              "optimizer_success": bool(result.success), "optimizer_message": result.message,
              "training": metrics(data[training, 1], prediction[training]),
              "holdout": metrics(data[validation, 1], prediction[validation]),
              "holdout_after_motor_changes": metrics(data[validation & transient, 1], prediction[validation & transient]),
              "zero_lag_holdout": metrics(data[validation, 1], no_lag[validation]),
              "fit_power_holdout": metrics(data[validation, 6], prediction[validation]),
              "training_position_range": [float(data[training, 3].min()), float(data[training, 3].max())],
              "training_cadence_range": [float(data[training, 2].min()), float(data[training, 2].max())],
              "stationary_variation": {"samples": int(stationary.sum()), "robust_sigma_w": fitted.noise_w,
                                       "lag_one_correlation": rho, "note": "Effective variation, including model error; not isolated meter noise."},
              "limitations": ["Chronological holdout is from the same ride, not an independent ride.",
                              "One-second logs cannot identify subsecond motor travel or uniquely separate meter and bike lag.",
                              "Rider load response, backlash and endstop torque are configurable assumptions, not calibrated measurements.",
                              "Unobserved positions/cadences are extrapolation; physical bounds are assumed."]}
    return {"bike": asdict(fitted), "identification": report}, prediction, no_lag
