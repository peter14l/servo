/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// https://w3c.github.io/encrypted-media/#navigator-extension

dictionary MediaKeySystemConfiguration {
  DOMString label = \ \;
  sequence<DOMString> initDataTypes = [];
  sequence<MediaKeySystemMediaCapability> audioCapabilities = [];
  sequence<MediaKeySystemMediaCapability> videoCapabilities = [];
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

partial interface Navigator {
  [SecureContext, Pref=\dom_eme_enabled\] Promise<MediaKeySystemAccess> requestMediaKeySystemAccess(DOMString keySystem, sequence<MediaKeySystemConfiguration> supportedConfigurations);
};

partial interface WorkerNavigator {
  [SecureContext, Pref=\dom_eme_enabled\] Promise<MediaKeySystemAccess> requestMediaKeySystemAccess(DOMString keySystem, sequence<MediaKeySystemConfiguration> supportedConfigurations);
};
