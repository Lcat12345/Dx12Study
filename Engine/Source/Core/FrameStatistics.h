// FrameStatistics.h : deterministic warm-up and fixed-count frame sampling.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

struct FrameSampleSummary
{
    size_t sampleCount        = 0;
    double medianMilliseconds = 0.0;
    double p95Milliseconds    = 0.0;
    double maxMilliseconds    = 0.0;
};

class FrameSampleCollector
{
public:
    FrameSampleCollector(size_t warmupFrames, size_t sampleCount);

    // Ignores the configured warm-up, then returns exactly one summary after
    // sampleCount finite, non-negative measurements have been collected.
    std::optional<FrameSampleSummary> AddSample(double milliseconds);
    void Reset();

    size_t WarmupFrames() const { return m_warmupFrames; }
    size_t TargetSampleCount() const { return m_targetSampleCount; }
    bool   IsComplete() const { return m_complete; }

private:
    size_t              m_warmupFrames     = 0;
    size_t              m_targetSampleCount = 0;
    size_t              m_seenFrames       = 0;
    bool                m_complete         = false;
    std::vector<double> m_samples;
};
