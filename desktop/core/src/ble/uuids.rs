//! BLE UUIDs for the quarzlampe service, mirrored from
//! `include/settings.h` (`Settings::BLE_SERVICE_UUID` /
//! `BLE_COMMAND_CHAR_UUID` / `BLE_STATUS_CHAR_UUID`). Keep these in sync by
//! hand - there's no shared source of truth between the firmware (C++) and
//! this crate.

use uuid::Uuid;

pub const SERVICE: Uuid = uuid::uuid!("d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b");
pub const COMMAND_CHAR: Uuid = uuid::uuid!("4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7");
pub const STATUS_CHAR: Uuid = uuid::uuid!("c5ad78b6-9b77-4a96-9a42-8e6e9a40c123");
