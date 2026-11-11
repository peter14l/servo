/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>

namespace SchannelGateway {

/// Starts the local Windows Schannel HTTP gateway on 127.0.0.1.
/// Returns true if the gateway started successfully or is already running.
bool Start(unsigned short port = 51820);

/// Stops the gateway and terminates the worker thread.
void Stop();

/// Returns the port number the gateway is listening on.
unsigned short GetPort();

/// Returns true if the URL targets a domain known to require legacy TLS ciphers
/// (e.g. sxuk.org IIS 8.5 with CBC/SHA-1 ciphers).
bool ShouldUseGateway(const std::string& url);

/// Returns true if the URL is an internal gateway wrapper URL.
bool IsGatewayUrl(const std::string& url);

/// Wraps an external HTTPS URL into a local gateway URL so it is fetched
/// via native Windows Schannel.
std::string WrapUrl(const std::string& originalUrl);

/// Unwraps a gateway URL back into the original external HTTPS URL
/// for display in the Omnibox.
std::string UnwrapUrl(const std::string& url);

} // namespace SchannelGateway
