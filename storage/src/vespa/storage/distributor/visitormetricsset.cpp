// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "visitormetricsset.h"
#include <vespa/metrics/loadmetric.hpp>
#include <vespa/metrics/summetric.hpp>

namespace storage {

using metrics::MetricSet;

VisitorMetricSet::VisitorMetricSet(MetricSet* owner)
    : MetricSet("visitor", "visitor", "", owner),
      latency("latency", "", "Latency of visitor (in ms)", this),
      failed("failed", "", "Number of visitors that failed or were aborted by the user", this)
{ }

VisitorMetricSet::~VisitorMetricSet() { }

MetricSet *
VisitorMetricSet::clone(std::vector<Metric::LP>& ownerList, CopyType copyType,
                        MetricSet* owner, bool includeUnused) const
{
    if (copyType == INACTIVE) {
        return MetricSet::clone(ownerList, INACTIVE, owner, includeUnused);
    }
    return (VisitorMetricSet*) (new VisitorMetricSet(owner))->assignValues(*this);
}

}

template class metrics::LoadMetric<storage::VisitorMetricSet>;
template class metrics::SumMetric<storage::VisitorMetricSet>;