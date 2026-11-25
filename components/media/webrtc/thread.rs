/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc::{Sender, channel};
use std::thread;

use log::error;
use servo_media_streams::MediaStreamType;

use crate::datachannel::{DataChannelEvent, DataChannelId, DataChannelInit, DataChannelMessage};
use crate::{
    BundlePolicy, DescriptionType, IceCandidate, IceServer, IceTransportPolicy, MediaStreamId,
    RtcpMuxPolicy, SdpType, SessionDescription, WebRtcBackend, WebRtcControllerBackend,
    WebRtcDataChannelResult, WebRtcError, WebRtcResult, WebRtcSignaller,
};

#[derive(Clone)]
/// Entry point for all client webrtc interactions.
pub struct WebRtcController {
    sender: Sender<RtcThreadEvent>,
}

impl WebRtcController {
    pub fn new<T: WebRtcBackend>(signaller: Box<dyn WebRtcSignaller>) -> Result<Self, WebRtcError> {
        let (sender, receiver) = channel();

        let t = WebRtcController { sender };

        let mut controller = T::construct_webrtc_controller(signaller, t.clone())?;

        thread::spawn(move || {
            while let Ok(event) = receiver.recv() {
                if !handle_rtc_event(&mut controller, event) {
                    // shut down event loop
                    break;
                }
            }
        });

        Ok(t)
    }

    /// Creates a controller for a peer connection that Content Security Policy
    /// has blocked. It completes the signalling dance locally, without creating
    /// an ICE agent or starting a GStreamer pipeline, so no candidates are
    /// gathered and no connectivity checks are attempted.
    pub fn new_blocked(_signaller: Box<dyn WebRtcSignaller>) -> Self {
        let (sender, receiver) = channel();

        let t = WebRtcController { sender };

        let mut controller = BlockedWebRtcController {
            next_data_channel_id: AtomicUsize::new(0),
        };

        thread::spawn(move || {
            while let Ok(event) = receiver.recv() {
                if !handle_rtc_event(&mut controller, event) {
                    // shut down event loop
                    break;
                }
            }
        });

        t
    }
    pub fn configure(
        &self,
        ice_servers: Vec<IceServer>,
        bundle_policy: BundlePolicy,
        ice_transport_policy: IceTransportPolicy,
        rtcp_mux_policy: RtcpMuxPolicy,
    ) {
        let _ = self.sender.send(RtcThreadEvent::ConfigureIceServers(
            ice_servers,
            bundle_policy,
            ice_transport_policy,
            rtcp_mux_policy,
        ));
    }
    pub fn set_remote_description(
        &self,
        desc: SessionDescription,
        cb: Box<dyn FnOnce() + Send + 'static>,
    ) {
        let _ = self
            .sender
            .send(RtcThreadEvent::SetRemoteDescription(desc, cb));
    }
    pub fn set_local_description(
        &self,
        desc: SessionDescription,
        cb: Box<dyn FnOnce() + Send + 'static>,
    ) {
        let _ = self
            .sender
            .send(RtcThreadEvent::SetLocalDescription(desc, cb));
    }
    pub fn add_ice_candidate(&self, candidate: IceCandidate) {
        let _ = self.sender.send(RtcThreadEvent::AddIceCandidate(candidate));
    }
    pub fn create_offer(&self, cb: Box<dyn FnOnce(SessionDescription) + Send + 'static>) {
        let _ = self.sender.send(RtcThreadEvent::CreateOffer(cb));
    }
    pub fn create_answer(&self, cb: Box<dyn FnOnce(SessionDescription) + Send + 'static>) {
        let _ = self.sender.send(RtcThreadEvent::CreateAnswer(cb));
    }
    pub fn add_stream(&self, stream: &MediaStreamId) {
        let _ = self.sender.send(RtcThreadEvent::AddStream(*stream));
    }
    pub fn create_data_channel(&self, init: DataChannelInit) -> Option<DataChannelId> {
        let (sender, receiver) = channel();
        let _ = self
            .sender
            .send(RtcThreadEvent::CreateDataChannel(init, sender));
        receiver.recv().unwrap()
    }
    pub fn send_data_channel_message(&self, id: &DataChannelId, message: DataChannelMessage) {
        let _ = self
            .sender
            .send(RtcThreadEvent::SendDataChannelMessage(*id, message));
    }
    pub fn close_data_channel(&self, id: &DataChannelId) {
        let _ = self.sender.send(RtcThreadEvent::CloseDataChannel(*id));
    }

    /// This should not be invoked by clients
    pub fn internal_event(&self, event: InternalEvent) {
        let _ = self.sender.send(RtcThreadEvent::InternalEvent(event));
    }

    pub fn quit(&self) {
        let _ = self.sender.send(RtcThreadEvent::Quit);
    }
}

/// A WebRTC backend used when Content Security Policy blocks RTC connections.
///
/// It keeps the JavaScript-facing state machine usable (so that pages can
/// complete the signalling dance) while making sure that no ICE agent is
/// created, no candidates are surfaced and no connectivity checks are sent.
struct BlockedWebRtcController {
    next_data_channel_id: AtomicUsize,
}

impl BlockedWebRtcController {
    fn description(ty: SdpType) -> SessionDescription {
        // A minimal SDP description is sufficient for script: descriptions are
        // only passed back through the signalling channel by the tests and by
        // applications, and no media or ICE candidates are ever negotiated.
        SessionDescription {
            type_: ty,
            sdp: concat!(
                "v=0\r\n",
                "o=- 0 0 IN IP4 127.0.0.1\r\n",
                "s=Servo CSP-blocked WebRTC connection\r\n",
                "t=0 0\r\n",
                "a=group:BUNDLE 0\r\n",
                "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n",
                "c=IN IP4 0.0.0.0\r\n",
                "a=mid:0\r\n",
                "a=sctp-port:5000\r\n"
            )
            .to_owned(),
        }
    }
}

impl WebRtcControllerBackend for BlockedWebRtcController {
    fn configure(
        &mut self,
        _: &[IceServer],
        _: BundlePolicy,
        _: IceTransportPolicy,
        _: RtcpMuxPolicy,
    ) -> WebRtcResult {
        Ok(())
    }

    fn set_remote_description(
        &mut self,
        _: SessionDescription,
        cb: Box<dyn FnOnce() + Send + 'static>,
    ) -> WebRtcResult {
        cb();
        Ok(())
    }

    fn set_local_description(
        &mut self,
        _: SessionDescription,
        cb: Box<dyn FnOnce() + Send + 'static>,
    ) -> WebRtcResult {
        cb();
        Ok(())
    }

    fn add_ice_candidate(&mut self, _: IceCandidate) -> WebRtcResult {
        Ok(())
    }

    fn create_offer(
        &mut self,
        cb: Box<dyn FnOnce(SessionDescription) + Send + 'static>,
    ) -> WebRtcResult {
        cb(Self::description(SdpType::Offer));
        Ok(())
    }

    fn create_answer(
        &mut self,
        cb: Box<dyn FnOnce(SessionDescription) + Send + 'static>,
    ) -> WebRtcResult {
        cb(Self::description(SdpType::Answer));
        Ok(())
    }

    fn add_stream(&mut self, _: &MediaStreamId) -> WebRtcResult {
        Ok(())
    }

    fn create_data_channel(&mut self, _: &DataChannelInit) -> WebRtcDataChannelResult {
        Ok(self.next_data_channel_id.fetch_add(1, Ordering::Relaxed))
    }

    fn close_data_channel(&mut self, _: &DataChannelId) -> WebRtcResult {
        Ok(())
    }

    fn send_data_channel_message(
        &mut self,
        _: &DataChannelId,
        _: &DataChannelMessage,
    ) -> WebRtcResult {
        Ok(())
    }

    fn internal_event(&mut self, _: InternalEvent) -> WebRtcResult {
        Ok(())
    }

    fn quit(&mut self) {}
}

pub enum RtcThreadEvent {
    ConfigureIceServers(
        Vec<IceServer>,
        BundlePolicy,
        IceTransportPolicy,
        RtcpMuxPolicy,
    ),
    SetRemoteDescription(SessionDescription, Box<dyn FnOnce() + Send + 'static>),
    SetLocalDescription(SessionDescription, Box<dyn FnOnce() + Send + 'static>),
    AddIceCandidate(IceCandidate),
    CreateOffer(Box<dyn FnOnce(SessionDescription) + Send + 'static>),
    CreateAnswer(Box<dyn FnOnce(SessionDescription) + Send + 'static>),
    AddStream(MediaStreamId),
    CreateDataChannel(DataChannelInit, Sender<Option<DataChannelId>>),
    CloseDataChannel(DataChannelId),
    SendDataChannelMessage(DataChannelId, DataChannelMessage),
    InternalEvent(InternalEvent),
    Quit,
}

/// To allow everything to occur on the event loop,
/// the backend may need to send signals to itself
///
/// This is a somewhat leaky abstraction, but we don't
/// plan on having too many backends anyway
pub enum InternalEvent {
    OnNegotiationNeeded,
    OnIceCandidate(IceCandidate),
    OnAddStream(MediaStreamId, MediaStreamType),
    OnDataChannelEvent(DataChannelId, DataChannelEvent),
    DescriptionAdded(
        Box<dyn FnOnce() + Send + 'static>,
        DescriptionType,
        SdpType,
        /* remote offer generation */ u32,
    ),
    UpdateSignalingState,
    UpdateGatheringState,
    UpdateIceConnectionState,
}

pub fn handle_rtc_event(
    controller: &mut dyn WebRtcControllerBackend,
    event: RtcThreadEvent,
) -> bool {
    let result = match event {
        RtcThreadEvent::ConfigureIceServers(
            servers,
            bundle_policy,
            ice_transport_policy,
            rtcp_mux_policy,
        ) => controller.configure(
            &servers,
            bundle_policy,
            ice_transport_policy,
            rtcp_mux_policy,
        ),
        RtcThreadEvent::SetRemoteDescription(desc, cb) => {
            controller.set_remote_description(desc, cb)
        },
        RtcThreadEvent::SetLocalDescription(desc, cb) => controller.set_local_description(desc, cb),
        RtcThreadEvent::AddIceCandidate(candidate) => controller.add_ice_candidate(candidate),
        RtcThreadEvent::CreateOffer(cb) => controller.create_offer(cb),
        RtcThreadEvent::CreateAnswer(cb) => controller.create_answer(cb),
        RtcThreadEvent::AddStream(media) => controller.add_stream(&media),
        RtcThreadEvent::CreateDataChannel(init, sender) => controller
            .create_data_channel(&init)
            .map(|id| {
                let _ = sender.send(Some(id));
            })
            .inspect_err(|_| {
                let _ = sender.send(None);
            }),
        RtcThreadEvent::CloseDataChannel(id) => controller.close_data_channel(&id),
        RtcThreadEvent::SendDataChannelMessage(id, message) => {
            controller.send_data_channel_message(&id, &message)
        },
        RtcThreadEvent::InternalEvent(e) => controller.internal_event(e),
        RtcThreadEvent::Quit => {
            controller.quit();
            return false;
        },
    };
    if let Err(e) = result {
        error!("WebRTC backend encountered error: {:?}", e);
    }
    true
}
