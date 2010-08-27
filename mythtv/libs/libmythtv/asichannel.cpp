/** -*- Mode: c++ -*-
 *  Class ASIChannel
 */

// MythTV includes
#include "asichannel.h"

#define LOC     QString("ASIChan(%1): ").arg(GetDevice())
#define LOC_ERR QString("ASIChan(%1), Error: ").arg(GetDevice())

ASIChannel::ASIChannel(TVRec *parent, const QString &device) :
    DTVChannel(parent), m_device(device)
{
    m_tuner_types.push_back(DTVTunerType::kTunerTypeASI);
}

ASIChannel::~ASIChannel(void)
{
    Close();
}
