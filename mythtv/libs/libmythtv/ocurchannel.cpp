/** -*- Mode: c++ -*-
 *  Class OCURChannel
 */

#include <QCoreApplication>

// MythTV includes
#include "ocurchannel.h"
#include "mythcontext.h"
#include "upnpdevice.h"
#include "upnp.h"

#define LOC     QString("OCURChan(%1): ").arg(GetDevice())
#define LOC_ERR QString("OCURChan(%1), Error: ").arg(GetDevice())

// TODO CardUtil::IsSingleInputCard() returns true for OCUR tuners,
// but according to the API they can have multiple inputs.
// CardUtil::ProbeInputs() needs to be patched to support this
// in addition to the changes needed here.

OCURChannel::OCURChannel(TVRec *parent, const QString &device) :
    DTVChannel(parent), m_device(device)
{
    m_tuner_types.push_back(DTVTunerType::kTunerTypeOCUR);
}

OCURChannel::~OCURChannel(void)
{
    Close();
}

bool OCURChannel::Open(void)
{
    if (!m_upnp_usn.isEmpty())
        return true;

    if (!InitializeInputs())
        return false;

    QStringList dev = m_device.split(":");
    if (dev.size() < 2)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Invalid device, should be in the form uuid:recorder_number"); 
        return false;
    }

    QString uuid = dev[0];
    uint    num  = dev[1].toUInt();

    QString tuner_nt =
        QString("urn:schemas-opencable-com:service:Tuner:%1").arg(num);

    QString tuner_usn =
        QString("uuid:%1::urn:schemas-opencable-com:service:Tuner:%2")
        .arg(uuid).arg(num);

    VERBOSE(VB_CHANNEL, LOC + QString("NT  = %1").arg(tuner_nt));
    VERBOSE(VB_CHANNEL, LOC + QString("USN = %1").arg(tuner_usn));

    DeviceLocation *loc = UPnp::Find(tuner_nt, tuner_usn);
    if (loc)
    {
        VERBOSE(VB_IMPORTANT, LOC + "DeviceLocation: " + loc->toString());
        VERBOSE(VB_IMPORTANT, LOC + "Name&Details: " +
                loc->GetNameAndDetails());

        UPnpDeviceDesc *desc = loc->GetDeviceDesc(
            false /* TODO??? in qt thread */);
        if (desc)
        {
            m_upnp_nt  = tuner_nt;
            m_upnp_usn = tuner_usn;

            const UPnpDevice &upnpdev = desc->m_rootDevice;
            VERBOSE(VB_IMPORTANT, LOC + upnpdev.toString());

            //SOAPClient::Init();


            delete desc;
        }
        loc->Release();
    }

    return loc;
}

void OCURChannel::Close(void)
{
    m_upnp_nt.clear();
    m_upnp_usn.clear();
}

bool OCURChannel::SetChannelByString(const QString &channum)
{
    QString loc = LOC + QString("SetChannelByString(%1)").arg(channum);
    VERBOSE(VB_CHANNEL, loc);

    InputMap::const_iterator it = m_inputs.find(m_currentInputID);
    if (it == m_inputs.end())
        return false;

    uint mplexid_restriction;
    if (!IsInputAvailable(m_currentInputID, mplexid_restriction))
    {
        VERBOSE(VB_IMPORTANT, loc + " " + QString(
                    "Requested channel '%1' is on input '%2' "
                    "which is in a busy input group")
                .arg(channum).arg(m_currentInputID));

        return false;
    }

    // Fetch tuning data from the database.
    QString tvformat, modulation, freqtable, freqid, dtv_si_std;
    int finetune;
    uint64_t frequency;
    int mpeg_prog_num;
    uint atsc_major, atsc_minor, mplexid, tsid, netid;

    if (!ChannelUtil::GetChannelData(
        (*it)->sourceid, channum,
        tvformat, modulation, freqtable, freqid,
        finetune, frequency,
        dtv_si_std, mpeg_prog_num, atsc_major, atsc_minor, tsid, netid,
        mplexid, m_commfree))
    {
        VERBOSE(VB_IMPORTANT, loc + " " + QString(
                    "Requested channel '%1' is on input '%2' "
                    "which is in a busy input group")
                .arg(channum).arg(m_currentInputID));

        return false;
    }

    if (mplexid_restriction && (mplexid != mplexid_restriction))
    {
        VERBOSE(VB_IMPORTANT, loc + " " + QString(
                    "Requested channel '%1' is not available because "
                    "the tuner is currently in use on another transport.")
                .arg(channum));

        return false;
    }

    QString virtual_channel = freqid;

    bool ok = SetChannelByVirtualChannel(virtual_channel);

    if (ok)
    {
        m_curchannelname = channum;
        (*it)->startChanNum = channum;
    }

    VERBOSE(VB_CHANNEL, loc + " " + ((ok) ? "success" : "failure"));

    return ok;
}

bool OCURChannel::SetChannelByVirtualChannel(const QString &vchan)
{
    DeviceLocation *loc = UPnp::Find(m_upnp_nt, m_upnp_usn);
    if (!loc)
        return false;

    // TODO Channel change code goes here

    loc->Release();

    SetSIStandard("scte");
    SetDTVInfo(0,0,0,0,1);
    return true;
}
