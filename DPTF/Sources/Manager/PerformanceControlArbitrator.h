/******************************************************************************
** Copyright (c) 2013-2016 Intel Corporation All Rights Reserved
**
** Licensed under the Apache License, Version 2.0 (the "License"); you may not
** use this file except in compliance with the License.
**
** You may obtain a copy of the License at
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
** WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**
** See the License for the specific language governing permissions and
** limitations under the License.
**
******************************************************************************/

#pragma once

#include "Dptf.h"

//
// Arbitration Rule:
//
// The lowest p-state (highest index value) wins!!!
//

class dptf_export PerformanceControlArbitrator
{
public:

    PerformanceControlArbitrator();
    ~PerformanceControlArbitrator(void);

    // arbitrate() returns true if the arbitrated value has changed
    Bool arbitrate(UIntN policyIndex, UIntN performanceControlIndex);

    UIntN getArbitratedPerformanceControlIndex(void) const;
    void clearPolicyCachedData(UIntN policyIndex);

private:

    UIntN m_arbitratedPerformanceControlIndex;
    std::map<UIntN, UIntN> m_requestedPerformanceControlIndex;
};