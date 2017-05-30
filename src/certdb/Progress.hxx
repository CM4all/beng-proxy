/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PROGRESS_HXX
#define PROGRESS_HXX

#include <algorithm>

#include <stdio.h>

/**
 * An interface to Workshop job progress reporting.
 */
class WorkshopProgress {
    unsigned min = 0, max = 0;

public:
    WorkshopProgress() = default;

    constexpr WorkshopProgress(unsigned _min, unsigned _max)
        :min(_min), max(_max) {}

    constexpr WorkshopProgress(WorkshopProgress parent,
                               unsigned _min, unsigned _max)
        :min(parent.Scale(_min)), max(parent.Scale(_max)) {}

    bool IsEnabled() const {
        return min < max;
    }

    void operator()(int value) {
        if (IsEnabled())
            printf("%u\n", Scale(Clamp(value)));
    }

private:
    static constexpr unsigned Clamp(int x) {
        return std::min(100u, (unsigned)std::max(0, x));
    }

    constexpr unsigned Scale(unsigned x) const {
        return (min * (100u - x) + max * x) / 100u;
    }
};

/**
 * A simple wrapper for #WorkshopProgress which counts up to a
 * predefined number of steps.
 */
class StepProgress {
    WorkshopProgress parent;

    const unsigned n;

    unsigned i = 0;

public:
    StepProgress(WorkshopProgress _parent, unsigned _n)
        :parent(_parent), n(_n) {}

    void operator()() {
        ++i;
        parent(i * 100u / n);
    }
};

#endif
