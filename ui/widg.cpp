#include "widg.h"
#include "ui_widg.h"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "api/create_peerconnection_factory.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials.h"
#include "rtc_base/thread.h"
#include "rtc_base/logging.h"

ABSL_FLAG(
    std::string,
    force_fieldtrials,
    "",
    "Field trials control experimental features. This flag specifies the field "
    "trials in effect. E.g. running with "
    "--force_fieldtrials=WebRTC-FooFeature/Enabled/ "
    "will assign the group Enabled to field trial WebRTC-FooFeature. Multiple "
    "trials are separated by \"/\"");

widg::widg(QWidget *parent)
    : QWidget(parent), ui(new Ui::widg)
{
    ui->setupUi(this);
    absl::string_view field_trials_str = absl::GetFlag(FLAGS_force_fieldtrials);
    webrtc::FieldTrialsView *field_trials = nullptr;
    webrtc::Environment env = webrtc::CreateEnvironment();
    // webrtc::FieldTrials tmp(field_trials_str);
    // absl::string_view field_trials_str = "";
    // webrtc::Environment env =
    //     webrtc::CreateEnvironment(std::make_unique<webrtc::FieldTrials>(
    //         field_trials_str));

    m_ptrSignalingClient = new SignalingClient(this);
    m_ptrSignalingClient->connectToServer("ws://localhost:8000/server");
}

widg::~widg()
{
    delete ui;
}
