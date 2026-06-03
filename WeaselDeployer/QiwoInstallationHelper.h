#pragma once
#include <string>

/// 确保 Rime 的 installation.yaml 包含正确的同步配置。
/// installation_id 使用设备标识（与 WebDAV 同步配置中的 deviceId 一致），
/// sync_dir 指向 "sync/"，供 Rime 原生 sync_user_data() 机制使用。
class QiwoInstallationHelper {
 public:
  /// 确保 installation.yaml 存在且包含 installation_id 和 sync_dir。
  /// 如果 installation_id 已存在但不同，会替换并迁移旧的 sync 数据。
  static void Ensure(const std::string& rime_user_dir,
                     const std::string& device_id);

 private:
  static void MigrateSyncData(const std::string& rime_user_dir,
                              const std::string& old_id,
                              const std::string& new_id);
  static std::string MakeSafeId(const std::string& device_id);
  static std::string SyncDirName();
};
