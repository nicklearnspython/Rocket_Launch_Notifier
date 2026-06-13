// Alert Record persistence: one JSON file, atomic writes, UTC timestamps.
#pragma once

#include <filesystem>
#include <stdexcept>
#include <vector>

#include "rocket_watcher/domain.hpp"

namespace rocket_watcher {

class StorageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Abstract store so orchestration and tests never depend on the file format.
class AlertRecordStore {
 public:
  virtual ~AlertRecordStore() = default;

  // Throws StorageError if existing records cannot be read.
  virtual std::vector<AlertRecord> load() = 0;

  // Throws StorageError if records cannot be persisted.
  virtual void save(const std::vector<AlertRecord>& records) = 0;
};

class JsonAlertRecordStore : public AlertRecordStore {
 public:
  static constexpr int kFileVersion = 1;

  explicit JsonAlertRecordStore(std::filesystem::path path);

  const std::filesystem::path& path() const { return path_; }

  std::vector<AlertRecord> load() override;
  void save(const std::vector<AlertRecord>& records) override;

 protected:
  // Atomic-rename step, overridable in tests to simulate disk failures.
  virtual void replaceFile(const std::filesystem::path& temporary,
                           const std::filesystem::path& destination);

 private:
  std::filesystem::path path_;
};

}  // namespace rocket_watcher
