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

#include "DomainDisplayControl_001.h"
#include "XmlNode.h"

DomainDisplayControl_001::DomainDisplayControl_001(UIntN participantIndex, UIntN domainIndex, 
    std::shared_ptr<ParticipantServicesInterface> participantServicesInterface) :
    DomainDisplayControlBase(participantIndex, domainIndex, participantServicesInterface),
    m_userPreferredIndex(Constants::Invalid),
    m_lastSetDisplayBrightness(Constants::Invalid),
    m_isUserPreferredIndexModified(true),
    m_capabilitiesLocked(false)
{
    clearCachedData();
    capture();
}

DomainDisplayControl_001::~DomainDisplayControl_001(void)
{
    restore();
}

DisplayControlDynamicCaps DomainDisplayControl_001::getDisplayControlDynamicCaps(UIntN participantIndex, UIntN domainIndex)
{
    if (m_displayControlDynamicCaps.isInvalid())
    {
        m_displayControlDynamicCaps.set(createDisplayControlDynamicCaps(domainIndex));
    }
    return m_displayControlDynamicCaps.get();
}

DisplayControlStatus DomainDisplayControl_001::getDisplayControlStatus(UIntN participantIndex, UIntN domainIndex)
{
    Percentage brightnessPercentage = getParticipantServices()->primitiveExecuteGetAsPercentage(
        esif_primitive_type::GET_DISPLAY_BRIGHTNESS, domainIndex);

    m_currentDisplayControlIndex.set(getDisplayControlSet(participantIndex, domainIndex).getControlIndex(brightnessPercentage));

    return DisplayControlStatus(m_currentDisplayControlIndex.get());
}

UIntN DomainDisplayControl_001::getUserPreferredDisplayIndex(UIntN participantIndex, UIntN domainIndex)
{
    if ((getParticipantServices()->isUserPreferredDisplayCacheValid(participantIndex, domainIndex)) == true)
    {
        getParticipantServices()->writeMessageDebug(ParticipantMessage(
            FLF, "Attempting to get the user preferred index from the display cache."));
        m_userPreferredIndex = getParticipantServices()->getUserPreferredDisplayCacheValue(participantIndex, domainIndex);

        getParticipantServices()->writeMessageDebug(ParticipantMessage(
            FLF, "Retrieved the user preferred index of " + StlOverride::to_string(m_userPreferredIndex) + " ."));
        getParticipantServices()->invalidateUserPreferredDisplayCache(participantIndex, domainIndex);
    }
    else
    {
        auto currentStatus = getDisplayControlStatus(participantIndex, domainIndex);
        auto currentIndex = currentStatus.getBrightnessLimitIndex();

        if (m_userPreferredIndex == Constants::Invalid || 
            (m_lastSetDisplayBrightness != Constants::Invalid && currentIndex != m_lastSetDisplayBrightness))
        {
            m_userPreferredIndex = currentIndex;
            m_isUserPreferredIndexModified = true;
        }
        else
        {
            m_isUserPreferredIndexModified = false;
        }
    }

    return m_userPreferredIndex;
}

Bool DomainDisplayControl_001::isUserPreferredIndexModified(UIntN participantIndex, UIntN domainIndex)
{
    return m_isUserPreferredIndexModified;
}

DisplayControlSet DomainDisplayControl_001::getDisplayControlSet(UIntN participantIndex, UIntN domainIndex)
{
    if (m_displayControlSet.isInvalid())
    {
        m_displayControlSet.set(createDisplayControlSet(domainIndex));
    }
    return m_displayControlSet.get();
}

void DomainDisplayControl_001::setDisplayControl(UIntN participantIndex, UIntN domainIndex, UIntN displayControlIndex)
{
    auto indexToset = getAllowableDisplayBrightnessIndex(participantIndex, domainIndex, displayControlIndex);
    auto displaySet = getDisplayControlSet(participantIndex, domainIndex);
    Percentage newBrightness = displaySet[indexToset].getBrightness();

    m_lastSetDisplayBrightness = indexToset;

    getParticipantServices()->primitiveExecuteSetAsPercentage(
        esif_primitive_type::SET_DISPLAY_BRIGHTNESS,
        newBrightness,
        domainIndex);
}

UIntN DomainDisplayControl_001::getAllowableDisplayBrightnessIndex(UIntN participantIndex,
    UIntN domainIndex, UIntN requestedIndex)
{
    auto userPreferred = getUserPreferredDisplayIndex(participantIndex, domainIndex);
    auto dynamicCapabilities = getDisplayControlDynamicCaps(participantIndex, domainIndex);
    auto maxLimit = std::max(dynamicCapabilities.getCurrentUpperLimit(), userPreferred);

    auto valueToRequest = std::max(requestedIndex, maxLimit);

    auto minIndex = dynamicCapabilities.getCurrentLowerLimit();
    if (userPreferred <= minIndex)
    {
        valueToRequest = std::min(valueToRequest, minIndex);
    }

    return valueToRequest;
}

void DomainDisplayControl_001::setDisplayControlDynamicCaps(UIntN participantIndex, UIntN domainIndex, 
    DisplayControlDynamicCaps newCapabilities)
{
    auto displaySet = getDisplayControlSet(participantIndex, domainIndex);
    auto upperLimitIndex = newCapabilities.getCurrentUpperLimit();
    auto lowerLimitIndex = newCapabilities.getCurrentLowerLimit();

    if (upperLimitIndex != Constants::Invalid && lowerLimitIndex != Constants::Invalid)
    {
        auto size = displaySet.getCount();
        if (upperLimitIndex >= size)
        {
            throw dptf_exception("Upper Limit index is out of control set bounds.");
        }
        else if (upperLimitIndex > lowerLimitIndex || lowerLimitIndex >= size)
        {
            lowerLimitIndex = size - 1;
            getParticipantServices()->writeMessageWarning(
                ParticipantMessage(FLF, "Limit index mismatch, setting lower limit to lowest possible index."));
        }

        m_displayControlDynamicCaps.invalidate();

        Percentage upperLimitBrightness = displaySet[upperLimitIndex].getBrightness();
        UInt32 uint32UpperLimit = upperLimitBrightness.toWholeNumber();
        getParticipantServices()->primitiveExecuteSetAsUInt32(
            esif_primitive_type::SET_DISPLAY_CAPABILITY,
            uint32UpperLimit,
            domainIndex,
            Constants::Esif::NoPersistInstance);

        Percentage lowerLimitBrightness = displaySet[lowerLimitIndex].getBrightness();
        UInt32 uint32LowerLimit = lowerLimitBrightness.toWholeNumber();
        getParticipantServices()->primitiveExecuteSetAsUInt32(
            esif_primitive_type::SET_DISPLAY_DEPTH_LIMIT,
            uint32LowerLimit,
            domainIndex,
            Constants::Esif::NoPersistInstance);
    }
    else
    {
        m_displayControlDynamicCaps.invalidate();

        getParticipantServices()->primitiveExecuteSetAsUInt32(
            esif_primitive_type::SET_DISPLAY_CAPABILITY,
            upperLimitIndex,
            domainIndex,
            Constants::Esif::NoPersistInstance);

        getParticipantServices()->primitiveExecuteSetAsUInt32(
            esif_primitive_type::SET_DISPLAY_DEPTH_LIMIT,
            lowerLimitIndex,
            domainIndex,
            Constants::Esif::NoPersistInstance);
    }
}

void DomainDisplayControl_001::setDisplayCapsLock(UIntN participantIndex, UIntN domainIndex, Bool lock)
{
    m_capabilitiesLocked = lock;
}

void DomainDisplayControl_001::sendActivityLoggingDataIfEnabled(UIntN participantIndex, UIntN domainIndex)
{
    if (isActivityLoggingEnabled() == true) 
    {
        auto dynamicCaps = getDisplayControlDynamicCaps(participantIndex, domainIndex);
        auto displaySet = getDisplayControlSet(participantIndex, domainIndex);

        UInt32 displayControlIndex;
        if (m_currentDisplayControlIndex.isInvalid())
        {
            displayControlIndex = dynamicCaps.getCurrentUpperLimit();
        }
        else
        {
            displayControlIndex = m_currentDisplayControlIndex.get();
        }

        EsifCapabilityData capability;
        capability.type = ESIF_CAPABILITY_TYPE_DISPLAY_CONTROL;
        capability.size = sizeof(capability);
        capability.data.displayControl.currentDPTFLimit = displaySet[displayControlIndex].getBrightness().toWholeNumber();
        capability.data.displayControl.lowerLimit = displaySet[dynamicCaps.getCurrentLowerLimit()].getBrightness().toWholeNumber();
        capability.data.displayControl.upperLimit = displaySet[dynamicCaps.getCurrentUpperLimit()].getBrightness().toWholeNumber();

        getParticipantServices()->sendDptfEvent(ParticipantEvent::DptfParticipantControlAction,
            domainIndex, Capability::getEsifDataFromCapabilityData(&capability));
    }
}

void DomainDisplayControl_001::clearCachedData(void)
{
    m_displayControlDynamicCaps.invalidate();
    m_displayControlSet.invalidate();
    m_currentDisplayControlIndex.invalidate();

    if (m_capabilitiesLocked == false)
    {
        DptfBuffer capabilityBuffer = createResetPrimitiveTupleBinary(
            esif_primitive_type::SET_DISPLAY_CAPABILITY, Constants::Esif::NoPersistInstance);
        getParticipantServices()->primitiveExecuteSet(
            esif_primitive_type::SET_CONFIG_RESET, ESIF_DATA_BINARY,
            capabilityBuffer.get(), capabilityBuffer.size(), capabilityBuffer.size(), 
            0, Constants::Esif::NoInstance);

        DptfBuffer depthLimitBuffer = createResetPrimitiveTupleBinary(
            esif_primitive_type::SET_DISPLAY_DEPTH_LIMIT, Constants::Esif::NoPersistInstance);
        getParticipantServices()->primitiveExecuteSet(
            esif_primitive_type::SET_CONFIG_RESET, ESIF_DATA_BINARY,
            depthLimitBuffer.get(), depthLimitBuffer.size(), depthLimitBuffer.size(),
            0, Constants::Esif::NoInstance);
    }
}

std::shared_ptr<XmlNode> DomainDisplayControl_001::getXml(UIntN domainIndex)
{
    auto root = XmlNode::createWrapperElement("display_control");
    root->addChild(getDisplayControlStatus(getParticipantIndex(), domainIndex).getXml());
    root->addChild(getDisplayControlDynamicCaps(getParticipantIndex(), domainIndex).getXml());
    root->addChild(getDisplayControlSet(getParticipantIndex(), domainIndex).getXml());
    root->addChild(XmlNode::createDataElement("control_knob_version", "001"));

    return root;
}

void DomainDisplayControl_001::restore(void)
{
    if (m_userPreferredIndex != Constants::Invalid)
    {
        try
        {
            getParticipantServices()->setUserPreferredDisplayCacheValue(getParticipantIndex(), getDomainIndex(), m_userPreferredIndex);

            auto displaySet = getDisplayControlSet(getParticipantIndex(), getDomainIndex());
            auto upperLimitIndex = getDisplayControlDynamicCaps(getParticipantIndex(), getDomainIndex()).getCurrentUpperLimit();
            if (m_userPreferredIndex < upperLimitIndex)
            {
                m_userPreferredIndex = upperLimitIndex;
            }
            Percentage newBrightness = displaySet[m_userPreferredIndex].getBrightness();

            getParticipantServices()->writeMessageDebug(ParticipantMessage(
                FLF, "Saved the user preferred index of " + StlOverride::to_string(m_userPreferredIndex) + 
                ". Attempting to set the brightness to the user preferred value ."));

            getParticipantServices()->primitiveExecuteSetAsPercentage(
                esif_primitive_type::SET_DISPLAY_BRIGHTNESS,
                newBrightness,
                getDomainIndex());
        }
        catch (...)
        {
            // best effort
            getParticipantServices()->writeMessageDebug(ParticipantMessage(
                FLF, "Failed to restore the user preferred display status. "));
        }
    }
}

DisplayControlDynamicCaps DomainDisplayControl_001::createDisplayControlDynamicCaps(UIntN domainIndex)
{
    auto displaySet = getDisplayControlSet(getParticipantIndex(), domainIndex);

    // Get dynamic caps
    //  The caps are stored in BIOS as brightness percentage.  They must be converted
    //  to indices before they can be used.
    UIntN lowerLimitIndex = getLowerLimitIndex(domainIndex, displaySet);
    UIntN upperLimitIndex = getUpperLimitIndex(domainIndex, displaySet);

    auto size = displaySet.getCount();
    if (upperLimitIndex >= size)
    {
        throw dptf_exception("Upper Limit index is out of control set bounds.");
    }
    else if (upperLimitIndex > lowerLimitIndex || lowerLimitIndex >= size)
    {
        lowerLimitIndex = size - 1;
        getParticipantServices()->writeMessageWarning(
            ParticipantMessage(FLF, "Limit index mismatch, ignoring lower limit."));
    }

    return DisplayControlDynamicCaps(upperLimitIndex, lowerLimitIndex);
}

DisplayControlSet DomainDisplayControl_001::createDisplayControlSet(UIntN domainIndex)
{
    // _BCL Table
    DptfBuffer buffer = getParticipantServices()->primitiveExecuteGet(
        esif_primitive_type::GET_DISPLAY_BRIGHTNESS_LEVELS, ESIF_DATA_BINARY, domainIndex);
    auto displayControlSet = DisplayControlSet::createFromBcl(buffer);
    throwIfDisplaySetIsEmpty(displayControlSet.getCount());

    return displayControlSet;
}

void DomainDisplayControl_001::throwIfControlIndexIsOutOfRange(UIntN displayControlIndex, UIntN domainIndex)
{
    auto dynamicCaps = getDisplayControlDynamicCaps(getParticipantIndex(), domainIndex);
    auto upperLimit = dynamicCaps.getCurrentUpperLimit();
    auto lowerLimit = dynamicCaps.getCurrentLowerLimit();
    auto displaySet = getDisplayControlSet(getParticipantIndex(), domainIndex);
    auto size = displaySet.getCount();

    if (displayControlIndex >= size || displayControlIndex < upperLimit || displayControlIndex > lowerLimit)
    {
        std::stringstream infoMessage;

        infoMessage << "Control index is outside the allowable range." << std::endl
            << "Desired Index : " << displayControlIndex << std::endl
            << "DisplayControlSet size :" << size << std::endl
            << "Upper Limit : " << upperLimit << std::endl
            << "Lower Limit : " << lowerLimit << std::endl;

        throw dptf_exception(infoMessage.str());
    }
}

void DomainDisplayControl_001::throwIfDisplaySetIsEmpty(UIntN sizeOfSet)
{
    if (sizeOfSet == 0)
    {
        throw dptf_exception("Display Brightness set is empty. Impossible if we support display controls.");
    }
}

UIntN DomainDisplayControl_001::getLowerLimitIndex(UIntN domainIndex, DisplayControlSet displaySet)
{
    UInt32 uint32val = getParticipantServices()->primitiveExecuteGetAsUInt32(
        esif_primitive_type::GET_DISPLAY_DEPTH_LIMIT, domainIndex);
    Percentage lowerLimitBrightness = Percentage::fromWholeNumber(uint32val);
    return displaySet.getControlIndex(lowerLimitBrightness);
}

UIntN DomainDisplayControl_001::getUpperLimitIndex(UIntN domainIndex, DisplayControlSet displaySet)
{
    UIntN upperLimitIndex;
    try
    {
        UInt32 uint32val = getParticipantServices()->primitiveExecuteGetAsUInt32(
            esif_primitive_type::GET_DISPLAY_CAPABILITY, domainIndex);
        Percentage upperLimitBrightness = Percentage::fromWholeNumber(uint32val);
        upperLimitIndex = displaySet.getControlIndex(upperLimitBrightness);
    }
    catch (...)
    {
        // DDPC is optional
        getParticipantServices()->writeMessageDebug(
            ParticipantMessage(FLF, "DDPC was not present.  Setting upper limit to 100."));
        upperLimitIndex = 0; // Max brightness
    }
    return upperLimitIndex;
}

std::string DomainDisplayControl_001::getName(void)
{
    return "Display Control (Version 1)";
}