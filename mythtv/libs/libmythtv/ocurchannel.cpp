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

    bool using_cable_card = true;
    QString desired_service = (using_cable_card) ?
        QString("schemas-opencable-com:service:CAS:1") :
        QString("schemas-opencable-com:service:Tuner:1");


    QString tuner_nt  = QString("urn:%1").arg(desired_service);
    QString tuner_usn = QString("uuid:%1::%2").arg(uuid).arg(tuner_nt);

    VERBOSE(VB_CHANNEL, LOC + QString("NT  = %1").arg(tuner_nt));
    VERBOSE(VB_CHANNEL, LOC + QString("USN = %1").arg(tuner_usn));

    DeviceLocation *loc = UPnp::Find(tuner_nt, tuner_usn);
    if (loc)
    {
        UPnpDeviceDesc *desc = loc->GetDeviceDesc(
            true /* TODO??? in qt thread */);
        if (desc)
        {
            m_upnp_nt  = tuner_nt;
            m_upnp_usn = tuner_usn;

            const UPnpDevice &upnpdev = desc->m_rootDevice;
            VERBOSE(VB_IMPORTANT, LOC + upnpdev.toString());

            QString control_url =
                upnpdev.GetService(tuner_nt).m_sControlURL;

            QString control_path;
            if (control_url.startsWith("http://"))
            {
                QUrl url(control_url);
                control_path = url.path();
                url.setPath("");
                control_url = url.toString();
            }
            else
            {
                control_path = control_url;
                QUrl url(loc->m_sLocation);
                url.setPath("");
                control_url = url.toString();
            }

            if (control_path.left(1) != "/")
                control_path = "/" + control_path;

            //VERBOSE(VB_IMPORTANT, LOC + QString("Control URL: %1/%2")
            //        .arg(control_url).arg(control_path));

            SOAPClient::Init(control_url, tuner_nt, control_path);

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

    bool ok = false;
    if (m_upnp_nt.contains("CAS"))
    {
        uint32_t virtual_channel = freqid.toUInt();
        ok = SetChannelByVirtualChannel(virtual_channel);
    }
    else
    {
        // TODO handle Tuner service (full DTV tuning)
    }

    if (ok)
    {
        m_curchannelname = channum;
        (*it)->startChanNum = channum;
    }

    VERBOSE(VB_CHANNEL, loc + " " + ((ok) ? "success" : "failure"));

    return ok;
}

bool OCURChannel::SetChannelByVirtualChannel(uint32_t vchan)
{
    DeviceLocation *loc = UPnp::Find(m_upnp_nt, m_upnp_usn);
    if (!loc)
        return false;
    loc->Release();

    QString    method   = "SetChannel";
    QStringMap args;
    args["NewChannelNumber"] = QString::number(vchan);
    args["NewSourceId"]      = "0";
    args["NewCaptureMode"]   = "Live";
    int        err_code;
    QString    err_desc;
    bool ok = SendSOAPRequest(
        method, args, err_code, err_desc, true /*inQtThread*/);

    if (!ok)
    {
        VERBOSE(VB_IMPORTANT, QString("Error Code: %1\n\t\t\tDescription: %2")
                .arg(err_code).arg(err_desc));
    }
    else
    {
        SetSIStandard("scte");
        SetDTVInfo(0,0,0,0,1);
    }
    return ok;
}
