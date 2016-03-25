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
#include "DomainHardwareDutyCycleControlBase.h"

//
// Implements the Null Object pattern.  In the case that the functionality isn't implemented, we use
// this in place so we don't have to check for NULL pointers all throughout the participant implementation.
//

class DomainHardwareDutyCycleControl_000 : public DomainHardwareDutyCycleControlBase
{
public:

    DomainHardwareDutyCycleControl_000(UIntN participantIndex, UIntN domainIndex, 
        ParticipantServicesInterface* participantServicesInterface);

    // DomainHardwareDutyCycleControlInterface
    virtual DptfBuffer getHardwareDutyCycleUtilizationSet(
        UIntN participantIndex, UIntN domainIndex) const override;
    virtual Bool isEnabledByPlatform(UIntN participantIndex, UIntN domainIndex) const override;
    virtual Bool isSupportedByPlatform(UIntN participantIndex, UIntN domainIndex) const override;
    virtual Bool isEnabledByOperatingSystem(UIntN participantIndex, UIntN domainIndex) const override;
    virtual Bool isSupportedByOperatingSystem(UIntN participantIndex, UIntN domainIndex) const override;
    virtual Bool isHdcOobEnabled(UIntN participantIndex, UIntN domainIndex) const override;
    virtual void setHdcOobEnable(UIntN participantIndex, UIntN domainIndex, const UInt8& hdcOobEnable) override;
    virtual void setHardwareDutyCycle(UIntN participantIndex, UIntN domainIndex, const Percentage& dutyCycle) override;
    virtual Percentage getHardwareDutyCycle(UIntN participantIndex, UIntN domainIndex) const override;

    // ParticipantActivityLoggingInterface
    virtual void sendActivityLoggingDataIfEnabled(UIntN participantIndex, UIntN domainIndex) override;

    // ComponentExtendedInterface
    virtual void clearCachedData(void) override;
    virtual std::string getName(void) override;
    virtual std::shared_ptr<XmlNode> getXml(UIntN domainIndex) override;
};