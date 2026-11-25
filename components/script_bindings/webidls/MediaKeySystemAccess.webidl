/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// https://w3c.github.io/encrypted-media/#mediakeysystemaccess-interface

dictionary MediaKeySystemConfiguration {
  DOMString label = \ \;
  sequence<DOMString> initDataTypes = [];
  sequence<DOMString> audioCapabilities = [];
  sequence<DOMString> videoCapabilities = [];
  MediaKeysRequirement distinctiveIdentifier = \optional\;
  MediaKeysRequirement persistentState = \optional\;
  sequence<DOMString> sessionTypes = [];
};

enum MediaKeysRequirement { \required\, \optional\, \not-allowed\ };

dictionary MediaKeySystemMediaCapability {
  DOMString contentType = \\;
  DOMString robustness = \\;
  DOMString encryptionScheme = \\;
};

[Exposed=Window, SecureContext, Pref=\dom_eme_enabled\]
interface MediaKeySystemAccess {
  readonly attribute DOMString keySystem;
  MediaKeySystemConfiguration getConfiguration();
  Promise<MediaKeys> createMediaKeys();
};
