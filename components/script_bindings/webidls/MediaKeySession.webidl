/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// https://w3c.github.io/encrypted-media/#mediakeysession-interface

enum MediaKeySessionType { \ temporary\, \persistent-license\, \persistent-release-message\ };
enum MediaKeyMessageType { \license-request\, \license-renewal\, \license-release\, \individualization-request\ };
enum MediaKeyStatus { \usable\, \expired\, \released\, \output-restricted\, \output-downscaled\, \status-pending\, \internal-error\ };

dictionary MediaKeyMessageEventInit : EventInit {
  required MediaKeyMessageType messageType;
  required BufferSource message;
  DOMString destinationURL = \\;
};

[Exposed=Window, SecureContext, Pref=\dom_eme_enabled\]
interface MediaKeyMessageEvent : Event {
  constructor(DOMString type, MediaKeyMessageEventInit eventInitDict);
  readonly attribute MediaKeyMessageType messageType;
  readonly attribute ArrayBuffer message;
  readonly attribute DOMString destinationURL;
};

[Exposed=Window, SecureContext, Pref=\dom_eme_enabled\]
interface MediaKeyStatusMap {
  readonly attribute unsigned long size;
  boolean has(BufferSource keyId);
  any get(BufferSource keyId);
  iterable<BufferSource, MediaKeyStatus>;
  undefined forEach(Function callback, optional any thisArg);
};

[Exposed=Window, SecureContext, Pref=\dom_eme_enabled\]
interface MediaKeySession : EventTarget {
  readonly attribute DOMString sessionId;
  readonly attribute MediaKeyStatusMap keyStatuses;
  readonly attribute unrestricted double expiration;
  attribute EventHandler onkeystatuseschange;
  attribute EventHandler onmessage;
  Promise<undefined> generateRequest(BufferSource initDataType, BufferSource initData);
  Promise<undefined> load(DOMString sessionId);
  Promise<undefined> update(BufferSource response);
  Promise<undefined> close();
  Promise<undefined> remove();
};
