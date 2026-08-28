#pragma once

#include <cmath>

class MasterDeckSelector {
public:
    int Select(double balance, int leftDeck, int rightDeck) noexcept {
        if (leftDeck <= 0 && rightDeck <= 0) return 0;
        if (leftDeck <= 0) return selectedDeck_ = rightDeck;
        if (rightDeck <= 0) return selectedDeck_ = leftDeck;
        if (leftDeck == rightDeck) return selectedDeck_ = leftDeck;
        if (!std::isfinite(balance)) return CurrentOrNearest(leftDeck, rightDeck);

        if (selectedDeck_ != leftDeck && selectedDeck_ != rightDeck) {
            selectedDeck_ = balance < 0.5 ? leftDeck : rightDeck;
        } else if (selectedDeck_ == leftDeck && balance >= switchToRight_) {
            selectedDeck_ = rightDeck;
        } else if (selectedDeck_ == rightDeck && balance <= switchToLeft_) {
            selectedDeck_ = leftDeck;
        }
        return selectedDeck_;
    }

    int Current() const noexcept { return selectedDeck_; }

private:
    int CurrentOrNearest(int leftDeck, int rightDeck) noexcept {
        if (selectedDeck_ == leftDeck || selectedDeck_ == rightDeck) return selectedDeck_;
        return selectedDeck_ = leftDeck;
    }

    static constexpr double switchToLeft_ = 0.40;
    static constexpr double switchToRight_ = 0.60;
    int selectedDeck_{};
};
