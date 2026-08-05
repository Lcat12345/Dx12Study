#include "Core/FrameStatistics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

FrameSampleCollector::FrameSampleCollector(size_t warmupFrames,
                                           size_t sampleCount)
    : m_warmupFrames(warmupFrames)
    , m_targetSampleCount(sampleCount)
{
    if (sampleCount == 0)
    {
        throw std::invalid_argument("frame sample count must be greater than zero");
    }
    m_samples.reserve(sampleCount);
}

std::optional<FrameSampleSummary> FrameSampleCollector::AddSample(
    double milliseconds)
{
    if (m_complete)
    {
        return std::nullopt;
    }
    if (!std::isfinite(milliseconds) || milliseconds < 0.0)
    {
        throw std::invalid_argument("frame sample must be finite and non-negative");
    }

    if (m_seenFrames++ < m_warmupFrames)
    {
        return std::nullopt;
    }

    m_samples.push_back(milliseconds);
    if (m_samples.size() != m_targetSampleCount)
    {
        return std::nullopt;
    }

    std::vector<double> sorted = m_samples;
    std::sort(sorted.begin(), sorted.end());

    FrameSampleSummary summary;
    summary.sampleCount = sorted.size();
    const size_t middle = sorted.size() / 2;
    summary.medianMilliseconds = (sorted.size() % 2) != 0
        ? sorted[middle]
        : (sorted[middle - 1] + sorted[middle]) * 0.5;

    // Nearest-rank percentile: the smallest value whose cumulative rank is
    // at least 95%, matching how frame-time p95 is normally reported.
    const size_t p95Rank = size_t(std::ceil(0.95 * double(sorted.size())));
    summary.p95Milliseconds = sorted[(std::max)(size_t(1), p95Rank) - 1];
    summary.maxMilliseconds = sorted.back();
    m_complete = true;
    return summary;
}

void FrameSampleCollector::Reset()
{
    m_seenFrames = 0;
    m_complete = false;
    m_samples.clear();
}
