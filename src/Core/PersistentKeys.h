#pragma once

namespace NightMare::PersistentKey
{
// The topic root (address). Kept under its historical key.
inline constexpr char DeviceName[] = "_device_name";
// The display name, free text, not part of any topic.
inline constexpr char DeviceLabel[] = "_device_label";
// "2" once the address/label split has migrated this board's stored identity.
inline constexpr char IdentityModel[] = "_identity_model";
inline constexpr char DeviceTimezone[] = "_timezone";
// Namespace ID and topic root overrides. Present wins over NM_NAMESPACE / NM_TOPIC_ROOT; an
// empty _namespace is an explicit legacy choice.
inline constexpr char Namespace[] = "_namespace";
inline constexpr char TopicRoot[] = "_topic_root";
inline constexpr char PendingIdentityCleanup[] = "_pending_identity_cleanup";
inline constexpr char WifiSsid[] = "_ssid";
inline constexpr char WifiPassword[] = "_password";
inline constexpr char WifiTxPower[] = "_wifi_txpower";
inline constexpr char ConfigPrefix[] = "_config:";
}
