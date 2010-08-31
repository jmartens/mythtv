/** -*- Mode: c++ -*-
 *  Class OCURChannel
 */

// MythTV includes
#include "ocurchannel.h"

#define LOC     QString("OCURChan(%1): ").arg(GetDevice())
#define LOC_ERR QString("OCURChan(%1), Error: ").arg(GetDevice())

OCURChannel::OCURChannel(TVRec *parent, const QString &device) :
    DTVChannel(parent), m_device(device)
{
    m_tuner_types.push_back(DTVTunerType::kTunerTypeOCUR);
}

OCURChannel::~OCURChannel(void)
{
    Close();
}
