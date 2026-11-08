// SPDX-License-Identifier: GPL-3.0-or-later
#include "Calibration.h"

#include <cmath>

namespace game {

bool Calibration::press(double t) {
    if (t - last_press_ < kJumpS) return false;
    const double k = std::floor((t - first_) / period_ + 0.5);
    if (k < 0) return false;
    const double err = t - (first_ + k * period_);
    if (std::fabs(err) > kMaxErrorS) return false;
    last_press_ = t;
    errors_.push_back(err);
    if (errors_.size() > 4 * kWindow) errors_.erase(errors_.begin(), errors_.end() - kWindow);
    return true;
}

double Calibration::mean() const {
    const size_t n = count();
    if (!n) return 0;
    double sum = 0;
    for (size_t i = errors_.size() - n; i < errors_.size(); i++) sum += errors_[i];
    return sum / n;
}

double Calibration::stdDev() const {
    const size_t n = count();
    if (n < 2) return 0;
    const double m = mean();
    double sum = 0;
    for (size_t i = errors_.size() - n; i < errors_.size(); i++) sum += (errors_[i] - m) * (errors_[i] - m);
    return std::sqrt(sum / (n - 1));
}

}  // namespace game
