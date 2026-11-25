/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use malloc_size_of::malloc_size_of_is_0;
use serde::{Deserialize, Serialize};
use servo_base::generic_channel::{self, GenericCallback, GenericSend, GenericSender, SendResult};
use servo_base::id::WebViewId;
use servo_url::ImmutableOrigin;

use crate::cache_storage::CacheStorageThreadMessage;
use crate::client_storage::{ClientStorageThreadHandle, ClientStorageThreadMessage};
use crate::indexeddb::IndexedDBThreadMsg;
use crate::webstorage_thread::{OriginDescriptor, WebStorageThreadMsg, WebStorageType};

pub mod cache_storage;
pub mod client_storage;
pub mod indexeddb;
pub mod webstorage_thread;

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct StorageThreads {
    client_storage_thread: GenericSender<ClientStorageThreadMessage>,
    idb_thread: GenericSender<IndexedDBThreadMsg>,
    web_storage_thread: GenericSender<WebStorageThreadMsg>,
    cache_storage_thread: GenericSender<CacheStorageThreadMessage>,
}

impl StorageThreads {
    pub fn new(
        client_storage_thread: GenericSender<ClientStorageThreadMessage>,
        idb_thread: GenericSender<IndexedDBThreadMsg>,
        web_storage_thread: GenericSender<WebStorageThreadMsg>,
        cache_storage_thread: GenericSender<CacheStorageThreadMessage>,
    ) -> StorageThreads {
        StorageThreads {
            client_storage_thread,
            idb_thread,
            web_storage_thread,
            cache_storage_thread,
        }
    }

    pub fn persisted(
        &self,
        origin: ImmutableOrigin,
        sender: GenericCallback<Result<bool, String>>,
    ) -> SendResult {
        self.client_storage_thread
            .send(ClientStorageThreadMessage::Persisted { origin, sender })
    }

    pub fn persist(
        &self,
        origin: ImmutableOrigin,
        permission_granted: bool,
        sender: GenericCallback<Result<bool, String>>,
    ) -> SendResult {
        self.client_storage_thread
            .send(ClientStorageThreadMessage::Persist {
                origin,
                permission_granted,
                sender,
            })
    }

    pub fn estimate(
        &self,
        origin: ImmutableOrigin,
        sender: GenericCallback<Result<(u64, u64), String>>,
    ) -> SendResult {
        self.client_storage_thread
            .send(ClientStorageThreadMessage::Estimate { origin, sender })
    }

    pub fn client_storage_handle(&self) -> ClientStorageThreadHandle {
        self.client_storage_thread.clone().into()
    }

    // TODO: Consider changing to `webstorage_sites`
    pub fn webstorage_origins(&self, storage_type: WebStorageType) -> Vec<OriginDescriptor> {
        let (sender, receiver) = generic_channel::channel().unwrap();
        let _ = self
            .web_storage_thread
            .send(WebStorageThreadMsg::ListOrigins(sender, storage_type));
        receiver.recv().unwrap()
    }

    pub fn clear_webstorage_for_sites(&self, storage_type: WebStorageType, sites: &[&str]) {
        let sites = sites.iter().map(|site| site.to_string()).collect();
        let (sender, receiver) = generic_channel::channel().unwrap();
        let _ = self
            .web_storage_thread
            .send(WebStorageThreadMsg::ClearDataForSites(
                sender,
                storage_type,
                sites,
            ));
        let _ = receiver.recv();
    }

    /// Drop all ephemeral search storage when the last browsing context closes.
    /// This covers both localStorage and IndexedDB; cookies are purged through
    /// the network thread separately.
    pub fn purge_ephemeral_search_storage(&self) {
        let (web_storage_sender, web_storage_receiver) = generic_channel::channel().unwrap();
        let _ = self
            .web_storage_thread
            .send(WebStorageThreadMsg::PurgeEphemeralSearchStorage(
                web_storage_sender,
            ));
        let _ = web_storage_receiver.recv();

        let (indexeddb_sender, indexeddb_receiver) = generic_channel::channel().unwrap();
        let _ = self
            .idb_thread
            .send(IndexedDBThreadMsg::PurgeEphemeralSearchDatabases(
                indexeddb_sender,
            ));
        let _ = indexeddb_receiver.recv();
    }

    /// Drop one webview's in-memory search sessionStorage. Shared localStorage
    /// and IndexedDB remain alive until the last webview closes.
    pub fn purge_ephemeral_search_storage_for_webview(&self, webview_id: WebViewId) {
        let (sender, receiver) = generic_channel::channel().unwrap();
        let _ = self.web_storage_thread.send(
            WebStorageThreadMsg::PurgeEphemeralSearchStorageForWebView(webview_id, sender),
        );
        let _ = receiver.recv();
    }
}

impl GenericSend<ClientStorageThreadMessage> for StorageThreads {
    fn send(&self, msg: ClientStorageThreadMessage) -> SendResult {
        self.client_storage_thread.send(msg)
    }

    fn sender(&self) -> GenericSender<ClientStorageThreadMessage> {
        self.client_storage_thread.clone()
    }
}

impl GenericSend<IndexedDBThreadMsg> for StorageThreads {
    fn send(&self, msg: IndexedDBThreadMsg) -> SendResult {
        self.idb_thread.send(msg)
    }

    fn sender(&self) -> GenericSender<IndexedDBThreadMsg> {
        self.idb_thread.clone()
    }
}

impl GenericSend<WebStorageThreadMsg> for StorageThreads {
    fn send(&self, msg: WebStorageThreadMsg) -> SendResult {
        self.web_storage_thread.send(msg)
    }

    fn sender(&self) -> GenericSender<WebStorageThreadMsg> {
        self.web_storage_thread.clone()
    }
}

impl GenericSend<CacheStorageThreadMessage> for StorageThreads {
    fn send(&self, msg: CacheStorageThreadMessage) -> SendResult {
        self.cache_storage_thread.send(msg)
    }

    fn sender(&self) -> GenericSender<CacheStorageThreadMessage> {
        self.cache_storage_thread.clone()
    }
}

// Ignore the sub-fields
malloc_size_of_is_0!(StorageThreads);
