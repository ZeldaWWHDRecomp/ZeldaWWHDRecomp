// The save in iCloud through CloudKit, for the UIKit host (Apple TV, iPhone, iPad: cloud_save_uikit.mm). An Apple TV
// app gets no iCloud Drive, only CloudKit, so the save travels as records in the player's private database of the
// container the app's Info.plist names (WWHDCloudKitContainer): a manifest, and each distinct file content once
// (cloud_save_uikit.mm); without a container, nothing here runs.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace cloud_save {
using Ask = std::function<int(const std::string& title, const std::string& text, const std::vector<std::string>& buttons)>;

// main(), after the save folder is settled and before the game reads its save: brings the newer save here (the one in
// iCloud when only it changed since this device last synced, this one when only it changed; both changed: ASK), then
// keeps uploading this device's save while the game runs. Whatever is replaced is kept in save-backups.
void startup(const std::string& save_dir, const Ask& ask);

// the app is going to the background: upload a changed save now
void flush();
// the game is ending: upload a changed save (one written in the last moments too, once it is written), then DONE (on a
// thread of its own, not the main one: an app whose windows are gone may be suspended; at once when nothing changed,
// after at most about 20 s otherwise)
void flush_then(std::function<void()> done);
}  // namespace cloud_save
